#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang -std=c11 -Wall -Wextra -Werror -fsyntax-only -Itests/mock_sdk \
 applications_user/private_lab/private_lab.c applications_user/private_lab/obd_parser.c
clang -std=c11 -Wall -Wextra -Werror applications_user/private_lab/obd_parser.c \
 tests/test_obd.c -o /tmp/privatelab3_obd_test
/tmp/privatelab3_obd_test
python3 -m unittest discover -s tests -v
