## Steps for deploying prometheus and node-exporter on kubernetes cluster for getting  system metrics

## create namespace monitoring
kubectl create namespace monitoring

## deploy node-exporter and its service
kubectl create -f node-exporter.yml

kubectl create -f node-exporter-service.yml

## create a service account for prometheus
kubectl apply -f service-account.yml -n monitoring

## create prometheus config and store it as a configmap
kubectl create configmap prometheus-config -n monitoring --from-file=prometheus.yml --dry-run=client -o yaml | kubectl apply -f -

## deploy prometheus and its service
kubectl apply -f prometheus-deployment.yml
kubectl apply -f prometheus-service.yml

## configure prometheus to scrape node-exporter
kubectl apply -f prometheus-endpoints-role.yml
kubectl apply -f prometheus-endpoints-binding.yml

## delete the prometheus pod to force it to reload the config
kubectl delete pod -n monitoring -l app=prometheus

## launch prometheus dashboard
kubectl port-forward -n monitoring service/prometheus-service 9090:9090

## open dashborad in browser
http://localhost:9090
## use the promQL queries below as examples to get the required metrics

## cpu utilization
max =>
max((1-rate(node_cpu_seconds_total{mode="idle"}[1m]))*100)

p90 =>
quantile(0.9,(1-rate(node_cpu_seconds_total{mode="idle"}[1m]))*100)
 
cpu utilization => 
100-(avg(rate(node_cpu_seconds_total{mode="idle"}[1m])) * 100)

## disk usage
disk read :
avg by (device)(rate(node_disk_read_bytes_total[1m]))

disk write:
avg by (device)(rate(node_disk_read_bytes_total[1m]))


### network usage
in:
avg by (device)(rate(node_network_receive_bytes_total[1m]))
out:
avg by (device)(rate(node_network_transmit_bytes_total[1m]))

## mem usage
max(node_memory_MemTotal_bytes-node_memory_MemFree_bytes)
