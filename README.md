# Quant phone wallet for Termux (`qnt`)

A light wallet for **Quant (QNT)** that runs in [Termux](https://termux.dev) on Android. It
doesn't download the blockchain. It downloads only block headers (120 bytes each), checks the
proof-of-work and difficulty itself, and asks full nodes for your coins. Every coin arrives
with a Merkle proof tied to a header your phone has verified. Keys are created and used only on
your phone.

It finds nodes without any server: saved peers, nodes on your Wi-Fi (LAN broadcast), and the
public BitTorrent DHT.

## Install (in Termux)

```
pkg install git
git clone --recursive https://github.com/tehrealaju-svg/quant-termux
cd quant-termux && ./setup.sh
qnt
```
The first build takes a few minutes on a phone.

## Use

```
qnt                       # menu: balance / receive / send / history / contacts / peers / seed
qnt new                   # new wallet (24 seed words)
qnt restore               # use the SAME 24 words as your PC wallet -> same coins
qnt receive               # address + QR code in the terminal
qnt send <address|contact> 1.25
qnt contacts add pc tqnt1...
qnt --addnode=192.168.1.20:17337 balance   # point at your own PC / Pi node directly
```

### Using it with your PC or Raspberry Pi
Run `quant-qt` or `quantd` at home. On the same Wi-Fi the phone finds it automatically. Away
from home, it finds nodes through the DHT, or you can use `--addnode=<your public ip>:17337` if
you forwarded the port.

Seed words are compatible across `quant-qt`, `quantd` and `qnt`.

Data lives in `~/.quant-light/<network>/` (wallet.qwl, headers.dat, peers.txt).

> ⚠️ Experimental software, not audited. Uses testnet until mainnet launches.
