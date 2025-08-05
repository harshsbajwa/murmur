#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

APP_NAME="Murmur"
BACKUP_DIR="./backups"
LOG_FILE="./deploy.log"

log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1" | tee -a "$LOG_FILE"
}

success() {
    echo -e "${GREEN} $1${NC}" | tee -a "$LOG_FILE"
}

warning() {
    echo -e "${YELLOW} $1${NC}" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED} $1${NC}" | tee -a "$LOG_FILE"
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

setup_environment() {
    log "Setting up environment..."
    
    if [ ! -f .env ]; then
        cp .env.production .env
        warning "Created .env from .env.production template"
        warning "Please update .env with your actual configuration values"
        
        if ! grep -q "APP_KEY=base64:" .env; then
            log "Generating application key..."
            php artisan key:generate --no-interaction
            success "Application key generated"
        fi
    fi
    
    mkdir -p database storage/logs storage/app/public "$BACKUP_DIR"
    
    chmod 755 database storage
    chmod 644 database/database.sqlite 2>/dev/null || touch database/database.sqlite
    
    success "Environment setup complete"
}

build_frontend() {
    log "Building frontend assets..."
    
    log "Installing frontend dependencies..."
    pnpm install
    
    log "Running pre-build tasks..."
    pnpm run prebuild
    
    log "Building production assets..."
    pnpm run build
    
    success "Frontend assets built successfully"
}

build_containers() {
    log "Building Docker containers..."
    
    docker-compose build --no-cache
    
    success "Containers built successfully"
}

create_backup() {
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
    docker-compose down
    
    log "Starting containers..."
    docker-compose up -d
    
    log "Waiting for containers to be ready..."
    sleep 30
    
    log "Running database migrations..."
    docker-compose exec -T caddy php artisan migrate --force
    
    log "Optimizing caches..."
    docker-compose exec -T caddy php artisan config:cache
    docker-compose exec -T caddy php artisan route:cache
    docker-compose exec -T caddy php artisan view:cache
    
    log "Restarting queue workers..."
    docker-compose exec -T caddy php artisan queue:restart
    
    success "Application deployed successfully"
}

health_check() {
    log "Performing health checks..."
    
    sleep 10

    if curl -f -s http://localhost/health > /dev/null; then
        success "Application health check passed"
    else
        error "Application health check failed"
    fi
    
    if docker-compose ps | grep -q "Up"; then
        success "Containers are running"
    else
        error "Some containers are not running"
    fi

    log "Container status:"
    docker-compose ps
}

main() {
    log "Starting $APP_NAME deployment..."
    
    SKIP_BACKUP=false
    SKIP_BUILD=false
    
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
            --help)
                echo "Usage: $0 [options]"
                echo "Options:"
                echo "  --skip-backup    Skip database backup"
                echo "  --skip-build     Skip asset building"
                echo "  --help           Show this help"
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                ;;
        esac
    done

    check_requirements
    setup_environment
    
    if [ "$SKIP_BACKUP" = false ]; then
        create_backup
    else
        warning "Skipping backup as requested"
    fi
    
    if [ "$SKIP_BUILD" = false ]; then
        build_frontend
        build_containers
    else
        warning "Skipping build as requested"
    fi
    
    deploy_application
    health_check
    
    success "$APP_NAME deployment completed successfully!"
    log "Access at: http://localhost (or configured domain)"
    log "View logs with: docker-compose logs -f"
    log "Access container shell with: docker-compose exec caddy sh"
}

trap 'error "Deployment interrupted"' INT TERM

main "$@"
