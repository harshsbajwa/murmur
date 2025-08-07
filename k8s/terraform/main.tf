terraform {
  required_version = ">= 1.0"
  required_providers {
    local = {
      source  = "hashicorp/local"
      version = "~> 2.4"
    }
    null = {
      source  = "hashicorp/null"
      version = "~> 3.2"
    }
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.23"
    }
  }
}

# Variables
variable "cluster_name" {
  description = "Name of the K3s cluster"
  type        = string
  default     = "murmur-cluster"
}

variable "node_name" {
  description = "Name of the K3s node"
  type        = string
  default     = "murmur-node-1"
}

variable "app_namespace" {
  description = "Kubernetes namespace for Murmur application"
  type        = string
  default     = "murmur"
}

variable "domain" {
  description = "Domain name for the application"
  type        = string
  default     = "amneet.me"
}

variable "app_subdomain" {
  description = "Subdomain for Murmur application"
  type        = string
  default     = "murmur"
}

# Local values
locals {
  app_fqdn = "${var.app_subdomain}.${var.domain}"
  labels = {
    "app.kubernetes.io/name"     = "murmur"
    "app.kubernetes.io/version"  = "1.0.0"
    "app.kubernetes.io/part-of"  = "murmur-stack"
  }
}

# K3s Installation
resource "null_resource" "k3s_install" {
  provisioner "local-exec" {
    command = <<-EOT
      # Check if K3s is already running
      if ! sudo systemctl is-active --quiet k3s; then
        echo "Installing K3s..."
        curl -sfL https://get.k3s.io | INSTALL_K3S_EXEC="server --disable traefik --disable servicelb --node-name ${var.node_name}" sh -
        
        # Wait for K3s to be ready
        timeout 120 bash -c 'until kubectl get nodes --request-timeout=5s >/dev/null 2>&1; do sleep 5; done'
        
        # Set up kubeconfig for current user
        sudo cp /etc/rancher/k3s/k3s.yaml $HOME/.kube/config
        sudo chown $(id -u):$(id -g) $HOME/.kube/config
        chmod 600 $HOME/.kube/config
      else
        echo "K3s is already running"
      fi
    EOT
  }

  provisioner "local-exec" {
    when    = destroy
    command = <<-EOT
      if sudo systemctl is-active --quiet k3s; then
        echo "Uninstalling K3s..."
        sudo /usr/local/bin/k3s-uninstall.sh || true
      fi
    EOT
  }
}

# Wait for K3s to be ready
resource "null_resource" "wait_for_k3s" {
  depends_on = [null_resource.k3s_install]

  provisioner "local-exec" {
    command = "timeout 120 bash -c 'until kubectl get nodes --request-timeout=5s >/dev/null 2>&1; do sleep 5; done'"
  }
}

# Configure Kubernetes provider
provider "kubernetes" {
  config_path = "~/.kube/config"
}

# Create namespace
resource "kubernetes_namespace" "murmur" {
  depends_on = [null_resource.wait_for_k3s]

  metadata {
    name = var.app_namespace
    labels = merge(local.labels, {
      "name" = var.app_namespace
    })
  }
}

# Create persistent volume for SQLite database
resource "kubernetes_persistent_volume" "murmur_db_pv" {
  depends_on = [kubernetes_namespace.murmur]

  metadata {
    name = "murmur-db-pv"
    labels = local.labels
  }

  spec {
    capacity = {
      storage = "10Gi"
    }
    access_modes = ["ReadWriteOnce"]
    persistent_volume_reclaim_policy = "Retain"
    storage_class_name = "local-storage"

    persistent_volume_source {
      host_path {
        path = "/home/harsh/murmur-storage/database"
        type = "DirectoryOrCreate"
      }
    }
  }
}

# Create persistent volume claim for database
resource "kubernetes_persistent_volume_claim" "murmur_db_pvc" {
  depends_on = [kubernetes_persistent_volume.murmur_db_pv]

  metadata {
    name      = "murmur-db-pvc"
    namespace = kubernetes_namespace.murmur.metadata[0].name
    labels    = local.labels
  }

  spec {
    access_modes = ["ReadWriteOnce"]
    resources {
      requests = {
        storage = "10Gi"
      }
    }
    storage_class_name = "local-storage"
    volume_name = kubernetes_persistent_volume.murmur_db_pv.metadata[0].name
  }
}

# Create persistent volume for Redis data
resource "kubernetes_persistent_volume" "murmur_redis_pv" {
  depends_on = [kubernetes_namespace.murmur]

  metadata {
    name = "murmur-redis-pv"
    labels = local.labels
  }

  spec {
    capacity = {
      storage = "5Gi"
    }
    access_modes = ["ReadWriteOnce"]
    persistent_volume_reclaim_policy = "Retain"
    storage_class_name = "local-storage"

    persistent_volume_source {
      host_path {
        path = "/home/harsh/murmur-storage/redis"
        type = "DirectoryOrCreate"
      }
    }
  }
}

# Create persistent volume claim for Redis
resource "kubernetes_persistent_volume_claim" "murmur_redis_pvc" {
  depends_on = [kubernetes_persistent_volume.murmur_redis_pv]

  metadata {
    name      = "murmur-redis-pvc"
    namespace = kubernetes_namespace.murmur.metadata[0].name
    labels    = local.labels
  }

  spec {
    access_modes = ["ReadWriteOnce"]
    resources {
      requests = {
        storage = "5Gi"
      }
    }
    storage_class_name = "local-storage"
    volume_name = kubernetes_persistent_volume.murmur_redis_pv.metadata[0].name
  }
}

# Create storage directories
resource "null_resource" "create_storage_dirs" {
  depends_on = [kubernetes_namespace.murmur]

  provisioner "local-exec" {
    command = <<-EOT
      mkdir -p $HOME/murmur-storage/{database,redis,logs,storage}
      mkdir -p $HOME/murmur-storage/storage/{app/public,framework/{cache,sessions,testing,views},logs}
      chmod -R 755 $HOME/murmur-storage
    EOT
  }
}

# Outputs
output "cluster_info" {
  value = {
    cluster_name = var.cluster_name
    node_name    = var.node_name
    namespace    = kubernetes_namespace.murmur.metadata[0].name
    app_fqdn     = local.app_fqdn
  }
}

output "storage_info" {
  value = {
    database_pv  = kubernetes_persistent_volume.murmur_db_pv.metadata[0].name
    database_pvc = kubernetes_persistent_volume_claim.murmur_db_pvc.metadata[0].name
    redis_pv     = kubernetes_persistent_volume.murmur_redis_pv.metadata[0].name
    redis_pvc    = kubernetes_persistent_volume_claim.murmur_redis_pvc.metadata[0].name
  }
}