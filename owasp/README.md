# Nginx web server with Web Application Firewall using OWASP ModSecurity and CRS

## Build and Deploy
```bash
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64,linux/arm64 -t rfledesma/nginx-waf:latest -t rfledesma/nginx-waf:1.0.0 --push .
```