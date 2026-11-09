# Texel-family follow-up

This follow-up retains the prepared guest view formats and descriptor payloads.
It does not claim to resolve all numeric mismatches, visual artifacts or frame-rate limits.

## Secondary uniform handles

The narrow VertexB family now rejects descriptors with a secondary handle, matching
its supported single static handle contract. The unsupported path retains the
original renderer behavior. The actual policy regression failed one assertion
before the guard and passed all 22 assertions afterward.
