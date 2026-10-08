# Security

Please report security problems privately, not in a public issue or
discussion: use **Report a vulnerability** on the
[Security tab](https://github.com/railyai/raily-pin/security/advisories/new)
of this repository.

Known, documented limits of the current firmware (not vulnerabilities to
report again):

- The Raily GATT characteristics are declared open, but since firmware
  0.2.18-qa presses and acknowledgements count only on a Bluetooth-bonded
  link that the Raily server admitted, and restart or update entry needs a
  pass from the server. The update bootloader is not signed yet.
- A freshly flashed pin that nobody has bound yet can be bound by the first
  phone that connects to it. Bind your pin right after flashing it.
