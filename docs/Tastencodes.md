
curl -X POST http://172.16.0.198/api/touch -H "Content-Type: application/json" \
  -d '{"button":"power","duration":3100}'



# Standby on/off
    -d '{"button":"power","duration":3100}'

# Gebläselüftung on/off
    -d '{"button":"boost","duration":5100}'

# Tastensperre on/off
    -d '{"combo":["up","down"],"duration":5100}'

# Timer Settings on
    -d '{"button":"time","duration":5100}'

# 