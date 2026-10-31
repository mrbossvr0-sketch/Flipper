#include "../applications_user/private_lab/obd_parser.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    ObdLive r = {0};
    assert(obd_parse_pid_response("010C\r41 0C 1A F8\r>", 0x0C, &r));
    assert(r.rpm_ok && r.rpm == 1726);
    assert(obd_parse_pid_response("7E8 04 41 0D 28\r>", 0x0D, &r));
    assert(r.speed_kmh == 40);
    assert(obd_parse_pid_response("41 05 64\r>", 0x05, &r));
    assert(r.coolant_c == 60);
    assert(!obd_parse_pid_response("SEARCHING...\rNO DATA\r>", 0x0C, &r));
    assert(!obd_parse_pid_response("010C\r>", 0x0C, &r));
    puts("OBD parser tests passed");
}
