# SMTP server

```sh
swaks --to recipient@example.com \
      --from sender@example.com \
      --server 192.168.100.12:25999 \
      --header "Subject: Test" \
      --body "This is a test message" \
      --no-suppress-data
```

```
```
=== Trying 192.168.100.12:25999...
=== Connected to 192.168.100.12.
<-  220 smtp server Service ready
 -> EHLO ubuntu22jammy
<-  250 OK
 -> MAIL FROM:<sender@example.com>
<-  250 OK
 -> RCPT TO:<recipient@example.com>
<-  250 OK
 -> DATA
<-  354 Start mail input; end with <CRLF>.<CRLF>
 -> Date: Fri, 10 Apr 2026 16:35:01 +0000
 -> To: recipient@example.com
 -> From: sender@example.com
 -> Subject: Test from swaks
 -> Message-Id: <20260410163501.000334@ubuntu22jammy>
 -> X-Mailer: swaks v20201014.0 jetmore.org/john/code/swaks/
 -> 
 -> This is a test message
 -> 
 -> 
 -> .
<-  250 OK queued
 -> QUIT
<-  221 Service closing
=== Connection closed with remote host.
```
```
