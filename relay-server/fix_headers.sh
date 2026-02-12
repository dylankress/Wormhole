#!/bin/bash
# Add pthread.h to ticket_manager.h
sed -i '/#pragma once/a #ifndef _WIN32\n#include <pthread.h>\n#endif' ticket_manager.h 2>/dev/null || true

# Add pthread.h to rate_limiter.h  
sed -i '/#pragma once/a #ifndef _WIN32\n#include <pthread.h>\n#endif' rate_limiter.h 2>/dev/null || true

# Add pthread.h and errno.h to server.h
sed -i '/#pragma once/a #ifndef _WIN32\n#include <pthread.h>\n#endif' server.h 2>/dev/null || true

# Add errno.h and string.h to server.c
sed -i '/#include <signal.h>/a #include <errno.h>\n#include <string.h>' server.c 2>/dev/null || true

# Add string.h to peer_registry.c
sed -i '1a #define _GNU_SOURCE' peer_registry.c 2>/dev/null || true

# Add string.h to ticket_manager.c
sed -i '1a #define _GNU_SOURCE' ticket_manager.c 2>/dev/null || true

echo "Headers fixed"
