## AYRIA (rewrite, work in progress)

**Currently reworking internal repos into something fit for public release.**

### Overview
AYRIA is a collection of tools for extending or replacing existing middlewares and platforms.
At its core it's just database replication/consensus over a decentralized network. With plugins
providing the integration with specific systems. e.g. Platformwrapper is targeting game-platforms
and implements Steam's and Tencent's APIs to act as a drop-in replacement for those platforms
during development.

### Authentication

Without a central authority the users are identified by their ECC public-key. This key is tied to
the users HWID or credentials depending on their preference. All messages and database updates
are signed with the users key to allow others to forward messages while retaining integrity.

### 





### Sponsors
This rewrite is primarilly sponsored by Hedgehogscience and the donations of our patreons.
Thank you all for your support <3.
