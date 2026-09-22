#!/bin/bash
# correlaciona leituras do controlador com keycodes e OBF no trace
cd /tmp || exit 1
F=qtrace.log
show() {
  awk '
    /pckbd_kbd_write_command/ { print NR": write_cmd 0x"$NF }
    /pckbd_kbd_write_data/    { print NR": write_data 0x"$NF }
    /ps2_put_keycode/         { print NR": PUT 0x"$NF }
    /ps2_read_data/           { print NR": ps2_read_data 0x"$NF }
    /pckbd_kbd_read_data/     { print NR": CTRL_READ 0x"$NF }
    /pckbd_kbd_read_status 0x1d/ { print NR": OBF status" }
  ' "$F"
}
show | grep -aE 'PUT|CTRL_READ|ps2_read_data|write_data' | head -150
echo '--- OBF occurrences (status 0x1d) timeline:'
show | grep -a OBF | awk 'NR==1||NR%20==0{print}' | head -20
echo '--- out of reset: ps2_read_data total:'
grep -ca 'ps2_read_data' "$F"