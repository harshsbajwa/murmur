.PHONY: help build up down restart logs shell test deploy clean backup restore

help:
	@echo 'Usage: make [target]'
	@echo ''
	@echo 'Targets:'
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z_-]+:.*?## / {printf "  %-15s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

install:
	composer install
	pnpm install

build-assets:
	pnpm run prebuild
	pnpm run build

dev:
	composer dev

build:
	docker-compose build --no-cache

up:
	docker-compose up -d

down:
	docker-compose down

restart:
	docker-compose restart

logs:
	docker-compose logs -f

logs-app:
	docker-compose logs -f caddy

logs-queue:
	docker-compose logs -f queue-worker

shell:
	docker-compose exec caddy sh

shell-worker:
	docker-compose exec queue-worker sh

redis-cli:
	docker-compose exec redis redis-cli

migrate:
	docker-compose exec caddy php artisan migrate --force

migrate-fresh:
	docker-compose exec caddy php artisan migrate:fresh --force

seed:
	docker-compose exec caddy php artisan db:seed --force

cache-clear:
	docker-compose exec caddy php artisan cache:clear
	docker-compose exec caddy php artisan config:clear
	docker-compose exec caddy php artisan route:clear
	docker-compose exec caddy php artisan view:clear

cache-optimize:
	docker-compose exec caddy php artisan config:cache
	docker-compose exec caddy php artisan route:cache
	docker-compose exec caddy php artisan view:cache

queue-restart:
	docker-compose exec caddy php artisan queue:restart

queue-status:
	docker-compose exec caddy php artisan queue:monitor

test:
	docker-compose exec caddy php artisan test

test-frontend:
	pnpm run lint
	pnpm run types

deploy:
	@echo "Starting deployment..."
	@make build-assets
	@make build
	@make down
	@make up
	@sleep 10
	@make migrate
	@make cache-optimize
	@make queue-restart
	@echo "Deployment complete!"

health:
	@echo "Checking application health..."
	@curl -f http://localhost/health || echo "Application health check failed"
	@curl -f http://localhost:2019/metrics || echo "Caddy metrics not available"

status:
	docker-compose ps

backup:
	@echo "Creating backup..."
	@mkdir -p backups
	@docker-compose exec caddy cp /app/database/database.sqlite /tmp/backup-$(shell date +%Y%m%d_%H%M%S).sqlite
	@docker cp $$(docker-compose ps -q caddy):/tmp/backup-$(shell date +%Y%m%d_%H%M%S).sqlite ./backups/
	@echo "Backup created in ./backups/"

restore:
	@if [ -z "$(BACKUP)" ]; then echo "Specify BACKUP=filename"; exit 1; fi
	@echo "Restoring from backup: $(BACKUP)"
	@docker cp ./backups/$(BACKUP) $$(docker-compose ps -q caddy):/app/database/database.sqlite
	@echo "Database restored from $(BACKUP)"

monitoring-up:
	docker-compose --profile monitoring up -d prometheus grafana

monitoring-down:
	docker-compose --profile monitoring down

clean:
	docker-compose down -v
	docker system prune -f
	docker volume prune -f

clean-logs:
	docker-compose exec caddy sh -c "find /var/log -name '*.log' -type f -delete"
	docker-compose exec caddy sh -c "find /app/storage/logs -name '*.log' -type f -delete"

ssl-renew:
	docker-compose exec caddy caddy reload --config /etc/caddy/Caddyfile

security-scan:
	@echo "Running security scans..."
	composer audit
	pnpm audit

update-deps:
	composer update
	pnpm update

setup-production:
	@echo "Setting up production environment..."
	@if [ ! -f .env ]; then cp .env.production .env; echo "Created .env from .env.production"; fi
	@echo "Update .env with configuration values"
	@echo "Generate APP_KEY with: php artisan key:generate"