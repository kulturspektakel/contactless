#!/bin/bash

echo "${SFTP_KEY}" | base64 --decode >/tmp/sftp_rsa
curl --ftp-create-dirs -T ${TRAVIS_BUILD_DIR}/.pio/build/signed.bin --key /tmp/sftp_rsa sftp://${SFTP_USER}:${SFTP_PASSWORD}@kult.cash/var/www/virtual/kultursp/kult.cash/
