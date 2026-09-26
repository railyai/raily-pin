# Security

Please report security problems privately, not in a public issue or
discussion: use **Report a vulnerability** on the
[Security tab](https://github.com/railyai/raily-pin/security/advisories/new)
of this repository.

Known, documented limits of the current firmware (not vulnerabilities to
report again):

- The Raily GATT service uses open (Just Works) access without bonding.
  Writes are rate-limited in firmware; bonded writes are planned.
- A freshly flashed pin that nobody has bound yet can be bound by the first
  phone that connects to it. Bind your pin right after flashing it.
