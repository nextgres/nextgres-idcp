# NEXTGRES In-Database Connection Pool Extension

## Overview
This **ALPHA** release of the NEXTGRES PostgreSQL extension introduces in-database connection pooling to significantly enhance PostgreSQL's ability to handle large numbers of concurrent connections. This solution integrates advanced connection pooling directly within PostgreSQL, eliminating the need for external poolers like PgBouncer and reducing latency issues.

## Features
- **In-Database Connection Pooling** - Reuses existing connections, conserving resources and minimizing the need to spawn new processes for each connection.
- **Enhanced Resource Efficiency** - Reduces memory and CPU usage by decreasing the number of processes and connections.
- **Improved Load Management** - Distributes incoming connections intelligently across the available pool to enhance performance during peak loads.

## Architecture
- **Background Worker-Based Connection Proxy** - Manages incoming connections before they reach the server, routing them to the most appropriate database session.
- **Embedded PgBouncer Elements** - Integrates elements of PgBouncer's connection management directly within the extension for improved efficiency.
- **Seamless Integration** - Designed as an embedded PostgreSQL extension, simplifying deployment and scalability without external dependencies.

## Installation and Usage
- Currently supports PostgreSQL 16, developed and tested on Ubuntu 22.04.
- This is an alpha release intended for early-performance testing and not for production use.

## Limitations
- Not designed for read/write load balancing or sharding.
- Lacks query cancellation, event loop cleanup, some GUCs are hardcoded.

## Future Developments
- Features under development include zero-copy packet handling, true multithreading, prioritization, and more optimized connection management features.

## Disclaimer
- This extension is in alpha stage. Do not use in production environments.

## License
- We are currently in a fundraising round, which licensing greatly affects. At present, we are releasing under a license derived from the Confluent Community License Agreement 1.0, found [here](LICENSE.md).

## Contact
- For support and more information, reach out to us at info@nextgres.com.

