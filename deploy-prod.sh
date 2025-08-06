#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

APP_NAME="Murmur"
BACKUP_DIR="./backups"
LOG_FILE="./deploy-prod.log"
ENV_FILE=".env.production"
COMPOSE_FILE="docker-compose.prod.yml"

log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1" | tee -a "$LOG_FILE"
}

success() {
    echo -e "${GREEN}✓ $1${NC}" | tee -a "$LOG_FILE"
}

warning() {
    echo -e "${YELLOW}⚠ $1${NC}" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED}✗ $1${NC}" | tee -a "$LOG_FILE"
    exit 1
}

check_requirements() {
    log "Checking requirements..."
    
    if ! command -v docker &> /dev/null; then
        error "Docker is not installed"
    fi
    
    if ! command -v docker-compose &> /dev/null; then
        error "Docker Compose is not installed"
    fi
    
    if ! command -v pnpm &> /dev/null; then
        error "pnpm is not installed"
    fi
    
    if ! command -v php &> /dev/null; then
        error "PHP is not installed"
    fi
    
    success "All requirements met"
}

setup_production_environment() {
    log "Setting up production environment..."
    
    if [ ! -f "$ENV_FILE" ]; then
        error "Production environment file $ENV_FILE not found"
    fi
    
    cp "$ENV_FILE" .env
    success "Production environment configuration loaded"
    
    mkdir -p database storage/logs storage/app/public "$BACKUP_DIR"
    
    chmod 755 database storage
    chmod 644 database/database.sqlite 2>/dev/null || touch database/database.sqlite
    chmod 644 database/database.sqlite
    
    success "Directory structure and permissions set"
}

build_frontend() {
    log "Building frontend assets..."
    
    log "Installing frontend dependencies..."
    pnpm install --frozen-lockfile
    
    log "Running pre-build tasks..."
    pnpm run prebuild
    
    log "Building production assets..."
    pnpm run build
    
    success "Frontend assets built successfully"
}

prepare_laravel() {
    log "Preparing Laravel for production..."
    
    log "Installing PHP dependencies..."
    composer install --optimize-autoloader --no-dev
    
    log "Running database migrations..."
    php artisan migrate --force
    
    log "Optimizing configuration..."
    php artisan config:cache
    php artisan route:cache
    php artisan view:cache
    
    success "Laravel prepared for production"
}

create_backup() {
    if [ "$SKIP_BACKUP" = true ]; then
        warning "Skipping backup as requested"
        return
    fi
    
    log "Creating backup..."
    
    mkdir -p "$BACKUP_DIR"
    BACKUP_NAME="backup-$(date +%Y%m%d_%H%M%S)"
    
    if [ -f database/database.sqlite ]; then
        cp database/database.sqlite "$BACKUP_DIR/$BACKUP_NAME.sqlite"
        success "Database backup created: $BACKUP_NAME.sqlite"
    else
        warning "No database file found to backup"
    fi
    
    if [ -d storage/app/public ]; then
        tar -czf "$BACKUP_DIR/$BACKUP_NAME-files.tar.gz" storage/app/public/
        success "Files backup created: $BACKUP_NAME-files.tar.gz"
    fi
}

deploy_application() {
    log "Deploying application..."
    
    log "Stopping existing containers..."
    docker-compose -f "$COMPOSE_FILE" down
    
    log "Building containers..."
    docker-compose -f "$COMPOSE_FILE" build --no-cache
    
    log "Starting containers..."
    docker-compose -f "$COMPOSE_FILE" up -d
    
    log "Waiting for containers to be ready..."
    sleep 30
    
    log "Running final optimizations..."
    docker-compose -f "$COMPOSE_FILE" exec -T caddy php artisan config:cache
    docker-compose -f "$COMPOSE_FILE" exec -T caddy php artisan route:cache
    docker-compose -f "$COMPOSE_FILE" exec -T caddy php artisan view:cache
    
    log "Restarting queue workers..."
    docker-compose -f "$COMPOSE_FILE" exec -T caddy php artisan queue:restart
    
    success "Application deployed successfully"
}

health_check() {
    log "Performing health checks..."
    
    sleep 15

    if curl -f -s http://localhost/health > /dev/null; then
        success "Application health check passed"
    else
        error "Application health check failed - please check logs"
    fi
    
    if docker-compose -f "$COMPOSE_FILE" ps | grep -q "Up"; then
        success "Containers are running"
    else
        error "Some containers are not running"
    fi

    log "Container status:"
    docker-compose -f "$COMPOSE_FILE" ps
    
    log "Application is accessible at:"
    log "  Web interface: http://localhost"
    log "  Health check: http://localhost/health"
    log "  Metrics (if enabled): http://localhost:2019/metrics"
}

show_logs() {
    log "Recent application logs:"
    docker-compose -f "$COMPOSE_FILE" logs --tail=20 caddy
}

cleanup_old_assets() {
    log "Cleaning up old assets..."
    
    rm -rf public/build/*
    rm -rf bootstrap/ssr/*
    
    success "Old assets cleaned"
}

main() {
    log "Starting $APP_NAME production deployment..."
    
    SKIP_BACKUP=false
    SKIP_BUILD=false
    SHOW_LOGS=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --skip-backup)
                SKIP_BACKUP=true
                shift
                ;;
            --skip-build)
                SKIP_BUILD=true
                shift
                ;;
            --show-logs)
                SHOW_LOGS=true
                shift
                ;;
            --help)
                echo "Usage: $0 [options]"
                echo "Options:"
                echo "  --skip-backup    Skip database backup"
                echo "  --skip-build     Skip asset building"
                echo "  --show-logs      Show recent logs after deployment"
                echo "  --help           Show this help"
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                ;;
        esac
    done

    check_requirements
    create_backup
    
    if [ "$SKIP_BUILD" = false ]; then
        cleanup_old_assets
        build_frontend
        prepare_laravel
    else
        warning "Skipping build as requested"
    fi
    
    setup_production_environment
    deploy_application
    health_check
    
    if [ "$SHOW_LOGS" = true ]; then
        show_logs
    fi
    
    success "$APP_NAME production deployment completed successfully!"
    echo
    echo "   Now running in production mode"
    echo "   Web interface: http://localhost"
    echo "   Health check: http://localhost/health"
    echo
    echo "   Monitoring:"
    echo "   Metrics: http://localhost:2019/metrics"  
    echo "   Prometheus: http://localhost:9090"
    echo "   Grafana: http://localhost:3000"
    echo
    echo "   Management:"
    echo "   View logs: docker-compose -f $COMPOSE_FILE logs -f"
    echo "   Shell access: docker-compose -f $COMPOSE_FILE exec caddy sh"
    echo "   Stop services: docker-compose -f $COMPOSE_FILE down"
}

trap 'error "Production deployment interrupted"' INT TERM

main "$@"