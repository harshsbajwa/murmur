#!/bin/bash

set -euo pipefail

# Murmur Production Deployment Script
# This script handles the complete production deployment of Murmur to K3s

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
NAMESPACE="murmur"
DEPLOYMENT_NAME="murmur-app"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

success() {
    echo -e "${GREEN}✓${NC} $1"
}

warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

error() {
    echo -e "${RED}✗${NC} $1" >&2
    exit 1
}

# Check prerequisites
check_prerequisites() {
    log "Checking prerequisites..."
    
    command -v kubectl >/dev/null 2>&1 || error "kubectl is not installed"
    command -v docker >/dev/null 2>&1 || error "docker is not installed" 
    command -v composer >/dev/null 2>&1 || error "composer is not installed"
    command -v pnpm >/dev/null 2>&1 || error "pnpm is not installed"
    
    # Check if cluster is accessible
    kubectl cluster-info >/dev/null 2>&1 || error "Cannot connect to Kubernetes cluster"
    
    success "Prerequisites check passed"
}

# Build application
build_application() {
    log "Building application..."
    
    cd "$PROJECT_ROOT"
    
    # Install PHP dependencies
    log "Installing PHP dependencies..."
    composer install --optimize-autoloader --no-dev --quiet
    
    # Install Node.js dependencies
    log "Installing Node.js dependencies..."
    pnpm install --silent
    
    # Build frontend assets
    log "Building frontend assets..."
    pnpm run prebuild
    pnpm run build
    
    # Optimize Laravel
    log "Optimizing Laravel..."
    php artisan optimize
    php artisan route:cache
    php artisan view:cache
    
    success "Application build completed"
}

# Setup storage
setup_storage() {
    log "Setting up storage directories..."
    
    STORAGE_DIR="$HOME/murmur-storage"
    
    mkdir -p "$STORAGE_DIR"/{database,redis,storage,logs}
    mkdir -p "$STORAGE_DIR/storage"/{app/public,framework/{cache,sessions,testing,views},logs}
    
    # Copy database if it doesn't exist
    if [[ ! -f "$STORAGE_DIR/database/database.sqlite" ]]; then
        if [[ -f "$PROJECT_ROOT/database/database.sqlite" ]]; then
            cp "$PROJECT_ROOT/database/database.sqlite" "$STORAGE_DIR/database/"
            log "Copied database to storage location"
        else
            warning "No existing database found, will create new one"
        fi
    fi
    
    success "Storage setup completed"
}

# Deploy to Kubernetes
deploy_to_k8s() {
    log "Deploying to Kubernetes..."
    
    cd "$PROJECT_ROOT"
    
    # Apply manifests
    kubectl apply -k k8s/manifests/ || error "Failed to apply Kubernetes manifests"
    
    # Wait for deployment to be ready
    log "Waiting for deployment to be ready..."
    kubectl wait --for=condition=available --timeout=300s deployment/$DEPLOYMENT_NAME -n $NAMESPACE
    
    success "Deployment to Kubernetes completed"
}

# Health check
health_check() {
    log "Performing health check..."
    
    # Get NodePort
    NODEPORT=$(kubectl get svc -n $NAMESPACE murmur-app -o jsonpath='{.spec.ports[0].nodePort}')
    NODE_IP=$(kubectl get nodes -o jsonpath='{.items[0].status.addresses[?(@.type=="InternalIP")].address}')
    
    # Check health endpoint
    for i in {1..10}; do
        if curl -s -f "http://$NODE_IP:$NODEPORT/health" > /dev/null; then
            success "Health check passed (http://$NODE_IP:$NODEPORT)"
            return 0
        fi
        log "Health check attempt $i/10 failed, retrying..."
        sleep 5
    done
    
    error "Health check failed after 10 attempts"
}

# Get deployment status
get_status() {
    log "Deployment Status:"
    echo
    
    kubectl get pods -n $NAMESPACE -o wide
    echo
    
    kubectl get svc -n $NAMESPACE
    echo
    
    kubectl get endpoints -n $NAMESPACE
    echo
    
    # Get NodePort info
    NODEPORT=$(kubectl get svc -n $NAMESPACE murmur-app -o jsonpath='{.spec.ports[0].nodePort}')
    NODE_IP=$(kubectl get nodes -o jsonpath='{.items[0].status.addresses[?(@.type=="InternalIP")].address}')
    
    echo -e "${GREEN}Application is accessible at:${NC}"
    echo -e "  Local: http://$NODE_IP:$NODEPORT"
    echo -e "  Health: http://$NODE_IP:$NODEPORT/health"
    echo
}

# Main deployment flow
main() {
    log "Starting Murmur production deployment..."
    
    check_prerequisites
    build_application
    setup_storage
    deploy_to_k8s
    health_check
    get_status
    
    success "Deployment completed successfully!"
}

# Handle command line arguments
case "${1:-deploy}" in
    "deploy")
        main
        ;;
    "build")
        build_application
        ;;
    "status")
        get_status
        ;;
    "health")
        health_check
        ;;
    *)
        echo "Usage: $0 [deploy|build|status|health]"
        echo "  deploy (default): Full deployment"
        echo "  build: Build application only"
        echo "  status: Show deployment status"
        echo "  health: Run health check"
        exit 1
        ;;
esac