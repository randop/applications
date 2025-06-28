# Boost ModSecurity project

### Install Dependencies (Ubuntu / Debian)
```bash
sudo apt update
sudo apt install -y libmodsecurity-dev libboost-all-dev meson build-essential
```

### Download and configure OWASP CRS
```bash
mkdir -p rules/owasp-crs
git clone https://github.com/coreruleset/coreruleset.git rules/owasp-crs
cp rules/owasp-crs/crs-setup.conf.example rules/owasp-crs/crs-setup.conf
```

### Configure the Build
```bash
meson setup build
```

### Compile
```bash
meson compile -C build
```

### Test rules
```bash
curl -X POST -d "data=<script>alert('xss')</script>" "http://localhost:8080/"
```