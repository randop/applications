#!/bin/sh

[ ! -e /tmp/svc.txt ] && touch /tmp/svc.txt
chmod a+r /tmp/svc.txt

INTERVAL=3
SERVICES="kea-dhcp4 pdns mariadb NetworkManager bird systemd-timesyncd dnsmasq"

alias_for() {
  case "$1" in
  kea-dhcp4) echo "DHCP(3)" ;;
  pdns) echo "DNS(4)" ;;
  mariadb) echo "DATABASE(5)" ;;
  bird) echo "BGP(7)" ;;
  dnsmasq) echo "ADBLOCK(9)" ;;
  NetworkManager) echo "NETWORK(6)" ;;
  systemd-timesyncd) echo "NTP(8)" ;;
  *) echo "$1" ;; # fallback: raw name
  esac
}

while true; do
  : >/tmp/svc.txt

  ping -c 1 -W 3 1.1.1.1 >/dev/null 2>&1 && echo "INTERNET(1),OK" >>/tmp/svc.txt || echo "INTERNET(1),BAD" >>/tmp/svc.txt

  ip link show enxe0 2>/dev/null | grep -q "state UNKNOWN" &&
    ip -4 addr show enxe0 2>/dev/null | grep -q "inet " &&
    echo "GATEWAY(2),OK" >>/tmp/svc.txt || echo "GATEWAY(2),BAD" >>/tmp/svc.txt

  for svc in $SERVICES; do
    if systemctl is-active --quiet "$svc"; then
      status="OK"
    else
      status="BAD"
    fi
    name=$(alias_for "$svc")
    echo "$name,$status" >>/tmp/svc.txt
  done

  ufw status | grep -q "Status: active" && echo "FIREWALL(10),OK" >>/tmp/svc.txt || echo "FIREWALL(10),BAD" >>/tmp/svc.txt

  ### padding
  # echo "NULL,NULL" >> /tmp/svc.txt

  sleep "$INTERVAL"
done
