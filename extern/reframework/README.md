# reframework plugin SDK (copied)

`API.h` and `API.hpp` are the REFramework plugin SDK headers, copied here
verbatim from upstream and compiled into `RE8HeadTracking.dll`. They are the
only way a REFramework plugin can talk to its host. Nothing in this directory
is ours.

Do not edit the headers. To move to a newer SDK, replace both files with the
upstream copies at the new commit and update the record below.

## Snapshot

- Upstream: https://github.com/praydog/REFramework
- Source commit: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- Files: `include/reframework/API.h`, `include/reframework/API.hpp`
- SDK version reported by the header: 1.15.0
- Licence: MIT, `Copyright (c) 2019 praydog`. Upstream `LICENSE` copied beside
  the headers and reproduced in `THIRD-PARTY-NOTICES.md`.

The same commit is what the vendored loader in `vendor/reframework/` was built
from, so the plugin builds against exactly the SDK the shipped loader
implements. Confirm that by reading `reframework_revision.txt` inside
`vendor/reframework/REFramework.zip`; it holds this SHA.

The commit recorded in `vendor/reframework/README.md` is a different thing: it
is the publishing commit in `praydog/REFramework-nightly`, the repository the
nightly release assets are attached to, and it is not a REFramework source
revision.

## Verifying the copies are unmodified

```bash
base=https://raw.githubusercontent.com/praydog/REFramework/ec6c81fd39831b328027ae00e102bc9c9c3f8aa5/include/reframework
curl -sfL "$base/API.h"   | diff -u - API.h
curl -sfL "$base/API.hpp" | diff -u - API.hpp
```

Both diffs are empty as of the last check.
