#!/bin/bash
grep -aoE 'KEY:'"'"'.'"'"'' /tmp/xk2.log | head -40
echo '--- prompt'
grep -a 'Novo admin' /tmp/xk2.log | tail -2
echo '--- telas'
grep -aE 'espere|Bem-|Incor|tente|conta|registr|salv|senha' /tmp/xk2.log | tail -8
echo '--- fim do serial'
tail -c 1200 /tmp/xk2.log | strings | tail -20