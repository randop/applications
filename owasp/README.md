# Nginx web server with Web Application Firewall using OWASP ModSecurity and CRS

## Build and Deploy
```bash
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64,linux/arm64 -t rfledesma/nginx-waf:latest -t rfledesma/nginx-waf:1.1.0 --push .
```

## Local Testing
```sh
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64 -t waf --load .
docker run --rm -it -p 8080:80 waf nginx -g "daemon off;"
```

#### SQL Injection test
```sh
curl -v "http://127.0.0.1:8080/?id=1+OR+1=1--"
```
```
2025/09/25 05:18:56 [error] 54#54: *3 [client 192.168.100.1] ModSecurity: Access denied with code 403 (phase 2). Matched "Operator `Ge' with parameter `5' against variable `TX:BLOCKING_INBOUND_ANOMALY_SCORE' (Value: `8' ) [file "/usr/local/owasp-modsecurity-crs/rules/REQUEST-949-BLOCKING-EVALUATION.conf"] [line "222"] [id "949110"] [rev ""] [msg "Inbound Anomaly Score Exceeded (Total Score: 8)"] [data ""] [severity "0"] [ver "OWASP_CRS/4.18.0"] [maturity "0"] [accuracy "0"] [tag "anomaly-evaluation"] [tag "OWASP_CRS"] [hostname "192.168.100.6"] [uri "/"] [unique_id "175877753683.729825"] [ref ""], client: 192.168.100.1, server: _, request: "GET /?id=1+OR+1=1-- HTTP/1.1", host: "192.168.100.6:8080"
192.168.100.1 - - [25/Sep/2025:05:18:56 +0000] "GET /?id=1+OR+1=1-- HTTP/1.1" 403 1381 "-" "curl/7.88.1" "-"
```

#### SQL Injection post test
```sh
curl -v -H "Content-Type: application/json" -d '{"id": "1\" OR \"1\"=\"1"}' "http://127.0.0.1:8080/"
```

```
2025/09/25 05:19:59 [error] 54#54: *4 [client 192.168.100.1] ModSecurity: Access denied with code 403 (phase 2). Matched "Operator `Ge' with parameter `5' against variable `TX:BLOCKING_INBOUND_ANOMALY_SCORE' (Value: `8' ) [file "/usr/local/owasp-modsecurity-crs/rules/REQUEST-949-BLOCKING-EVALUATION.conf"] [line "222"] [id "949110"] [rev ""] [msg "Inbound Anomaly Score Exceeded (Total Score: 8)"] [data ""] [severity "0"] [ver "OWASP_CRS/4.18.0"] [maturity "0"] [accuracy "0"] [tag "anomaly-evaluation"] [tag "OWASP_CRS"] [hostname "192.168.100.6"] [uri "/"] [unique_id "175877759921.412810"] [ref ""], client: 192.168.100.1, server: _, request: "POST / HTTP/1.1", host: "192.168.100.6:8080"
192.168.100.1 - - [25/Sep/2025:05:19:59 +0000] "POST / HTTP/1.1" 403 1381 "-" "curl/7.88.1" "-"
```

#### Cross-Site Scripting (XSS) test
```sh
curl -v "http://127.0.0.1:8080/?q=<script>alert('XSS')</script>"
```

```
2025/09/25 05:21:00 [error] 54#54: *5 [client 192.168.100.1] ModSecurity: Access denied with code 403 (phase 2). Matched "Operator `Ge' with parameter `5' against variable `TX:BLOCKING_INBOUND_ANOMALY_SCORE' (Value: `23' ) [file "/usr/local/owasp-modsecurity-crs/rules/REQUEST-949-BLOCKING-EVALUATION.conf"] [line "222"] [id "949110"] [rev ""] [msg "Inbound Anomaly Score Exceeded (Total Score: 23)"] [data ""] [severity "0"] [ver "OWASP_CRS/4.18.0"] [maturity "0"] [accuracy "0"] [tag "anomaly-evaluation"] [tag "OWASP_CRS"] [hostname "192.168.100.6"] [uri "/"] [unique_id "175877766010.309704"] [ref ""], client: 192.168.100.1, server: _, request: "GET /?q=<script>alert('XSS')</script> HTTP/1.1", host: "192.168.100.6:8080"
192.168.100.1 - - [25/Sep/2025:05:21:00 +0000] "GET /?q=<script>alert('XSS')</script> HTTP/1.1" 403 1381 "-" "curl/7.88.1" "-"
```

#### Local File Inclusion (LFI) test path traversal
```sh
curl -v "http://127.0.0.1:8080/?file=../../../etc/passwd"
```

```
2025/09/25 05:22:26 [error] 54#54: *6 [client 192.168.100.1] ModSecurity: Access denied with code 403 (phase 2). Matched "Operator `Ge' with parameter `5' against variable `TX:BLOCKING_INBOUND_ANOMALY_SCORE' (Value: `33' ) [file "/usr/local/owasp-modsecurity-crs/rules/REQUEST-949-BLOCKING-EVALUATION.conf"] [line "222"] [id "949110"] [rev ""] [msg "Inbound Anomaly Score Exceeded (Total Score: 33)"] [data ""] [severity "0"] [ver "OWASP_CRS/4.18.0"] [maturity "0"] [accuracy "0"] [tag "anomaly-evaluation"] [tag "OWASP_CRS"] [hostname "192.168.100.6"] [uri "/"] [unique_id "175877774659.121720"] [ref ""], client: 192.168.100.1, server: _, request: "GET /?file=../../../etc/passwd HTTP/1.1", host: "192.168.100.6:8080"
192.168.100.1 - - [25/Sep/2025:05:22:26 +0000] "GET /?file=../../../etc/passwd HTTP/1.1" 403 1381 "-" "curl/7.88.1" "-"
```

#### Sandbox Testing on the official CRS Sandbox
```sh
curl -H "x-format-output: txt-matched-rules" "https://sandbox.coreruleset.org/?search=<script>alert(1)</script>"
```

```
941100 PL1 XSS Attack Detected via libinjection
941110 PL1 XSS Filter - Category 1: Script Tag Vector
941160 PL1 NoScript XSS InjectionChecker: HTML Injection
941390 PL1 Javascript method detected
949110 PL1 Inbound Anomaly Score Exceeded (Total Score: 20)
980170 PL1 Anomaly Scores: (Inbound Scores: blocking=20, detection=20, per_pl=20-0-0-0, threshold=5) - (Outbound Scores: blocking=0, detection=0, per_pl=0-0-0-0, threshold=4) - (SQLI=0, XSS=20, RFI=0, LFI=0, RCE=0, PHPI=0, HTTP=0, SESS=0, COMBINED_SCORE=20)
```
