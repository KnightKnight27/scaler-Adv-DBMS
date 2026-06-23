#!/bin/bash
# Run this ONCE to zip reference code and send to teammates on WhatsApp

cd "$(dirname "$0")"
zip -r minidb_reference_for_teammates.zip reference_code/
echo ""
echo "Created: minidb_reference_for_teammates.zip"
echo "Send this zip to Member 2 and Member 3 on WhatsApp."
