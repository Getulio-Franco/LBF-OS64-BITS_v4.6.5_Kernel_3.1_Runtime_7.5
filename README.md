# LBF-OS64-BITS_v4.6.5_Kernel_3.1_Runtime_7.5
SISTEMA OPERACIONAL  x86-64 BITS

# 🌐 LBF Browser — TLS 1.3 nativo no LBF-OS64

> Um browser HTTP/HTTPS escrito do zero para o **LBF-OS64**: pilha **TLS 1.3 própria (RFC 8446)**, TCP/IP em user-space (Ring 3), parser HTML e decodificadores de imagem — **sem OpenSSL, sem mbedTLS, sem libc**.

*EN: A from-scratch HTTP/HTTPS browser for LBF-OS64 featuring a native TLS 1.3 stack (RFC 8446), a Ring 3 user-space TCP/IP stack, an HTML parser and PNG/BMP/GIF decoders. No third-party crypto libraries.*

---

## ✨ Destaques

- **TLS 1.3 completo**: ClientHello → ServerHello → voo cifrado (EE/Cert/CV/Finished) → **Finished do cliente** → dados de aplicação AES-128-GCM-SHA256
- **Criptografia própria**: X25519 (Montgomery ladder), AES-GCM, SHA-256, HMAC, HKDF/HKDF-Expand-Label, CSPRNG (RDRAND + jitter + HMAC-DRBG)
- **Camada de registro** com skip de ChangeCipherSpec (middlebox compat, §D.4), nonces XOR-seq (§5.2) e limites anti-overflow
- **Pilha TCP/IP em Ring 3**: ARP, DHCP, DNS, TCP com segmentação por MSS e buffers de 16 KB
- **Fallback automático** HTTPS→HTTP e cadeia de redirects (301/302/307)
- **Parser HTML**: links clicáveis, títulos, entidades PT-BR, UTF-8, skip de `<script>/<style>/<svg>`
- **Imagens inline**: PNG 8-bit (sem interlace), BMP 24/32, GIF (1º frame, LZW + interlace)
- **GUI multi-janela** com restauração de foco, histórico, SALVAR em FAT
- **Metodologia "olhos"**: instrumentação via Kernel Live Debug (`[REC] [HS] [KEY] [HKDF] [TLS_SYS]`) com versões `.dbg` arquivadas

## 🏗️ Arquitetura

```
┌────────────────────────────────────────────────────────────┐
│  browser.c (v6.5)  — GUI, net_fetch, parser, imagens       │
├────────────────────────────────────────────────────────────┤
│  sys_tls13.c       — dispatcher de syscalls (8 sessões)    │
├────────────────────────────────────────────────────────────┤
│  Core_tls13/                                                 │
│    tls13_handshake.c  máquina de estados + Finished        │
│    tls13_record.c     record layer AES-128-GCM             │
│    tls13.c            transcript + key schedule (§7.1)     │
├────────────────────────────────────────────────────────────┤
│  crypto/  hkdf • hmac • sha256 • aes_gcm • x25519 • rng     │
├────────────────────────────────────────────────────────────┤
│  net_user/  tcp • socket • ip • arp • dhcp • dns • poll     │
└────────────────────────────────────────────────────────────┘
```

## 📁 Estrutura

```
Runtime_Net/
├── Software/browser.c          # app: GUI + fetch HTTP/TLS + render
├── sys_tls13/sys_tls13.c       # bridge Ring3 ↔ core (CONNECT/READ/WRITE/CLOSE)
├── Core_tls13/
│   ├── tls13.h                 # tipos, constantes, protótipos
│   ├── tls13.c                 # sessão, transcript, key schedule, Finished
│   ├── tls13_handshake.c       # ClientHello/ServerHello/mensagens coalescidas
│   └── tls13_record.c          # write/read de registros, CCS skip, AES-GCM
├── crypto/                     # hkdf.c, hmac, sha256, aes_gcm, x25519, rng
├── net_user/                   # tcp.c, socket.c, ip, arp, dhcp, dns, net_poll
├── web_html.c                  # construtor WebDoc (linhas/links/imagens)
└── web_img.c                   # decodificadores PNG/BMP/GIF
```

## 🔐 Handshake TLS 1.3 como implementado

1. **ClientHello** com ordem canônica de extensões: SNI → supported_groups (x25519) → signature_algorithms → ALPN (`http/1.1`) → supported_versions (0x0304) → key_share
2. **ServerHello**: validação de suíte (0x1301), extração do key_share, transcript limitado a `4+msg_len`
3. **Key schedule** (§7.1): early → derived → handshake secret → traffic secrets → chaves/IVs AEAD
4. **Voo cifrado**: EncryptedExtensions, Certificate, CertificateVerify e Finished parseados **coalescidos** (1 registro = N mensagens); CCS descartado
5. **Finished do cliente** (§4.4.4) enviado com as chaves de handshake; depois, derivação das chaves de aplicação
6. **App data**: GET cifrado → resposta decifrada registro a registro (`inner type 23`)

## 🚀 Build

Requisitos: gcc x86-64 freestanding, LBF-OS64 SDK (libgui/IPC), target VirtualBox/hardware real.

```bash
# 1) Engine TLS (objeto combinado)
for f in Runtime_Net/crypto/*.c Runtime_Net/Core_tls13/*.c Runtime_Net/sys_tls13/*.c; do
  gcc -O3 -msse3 -m64 -ffreestanding -fno-stack-protector -fno-pie \
      -Isystem -I. -IRuntime_Net/crypto -IRuntime_Net/Core_tls13 \
      -c "$f" -o "${f%.c}.o"
done
ld -r Runtime_Net/crypto/*.o Runtime_Net/Core_tls13/*.o \
     Runtime_Net/sys_tls13/sys_tls13.o -o Runtime_Net/lib_tls13/tls13_engine.o

# 2) Browser
gcc -O3 -msse3 -m64 -ffreestanding -fno-stack-protector -fno-pie \
    -Isystem -I. -IRuntime_sdk/sdk -IRuntime_Net/sys_tls13 \
    -c Runtime_Net/Software/browser.c -o Runtime_Net/Software/browser.o

# 3) Relink do browsere.elf + mcopy para o disco do OS
```

## 🧪 Testado contra (out/2026)

| Site | TLS 1.3 | Observação |
| :--- | :---: | :--- |
| `www.google.com` | ✅ | homepage + imagens |
| `policies.google.com/terms` | ✅ | HTTPS-only (sem fallback possível) |
| `github.com` | ✅ | resposta chunked decifrada |
| `www.wikipedia.org` | ✅ | redirect http→https seguido |
| `panel.dreamhost.com` | ✅ | imagem via 2º handshake TLS |
| `example.com` | ✅ | vetor de regressão da campanha |

## ⚠️ Limitações atuais (honestas)

- **Sem validação de certificado** — conexão cifrada, mas sem autenticação de identidade (não use para dados sensíveis)
- Sem JavaScript, cookies ou POST/upload (somente GET)
- HTTP/1.1 apenas (ALPN `http/1.1`); sem HTTP/2
- TLS 1.3 apenas — servidores ≤1.2 caem no fallback HTTP quando disponível
- Respostas até 200 KB; ≤3 imagens inline (≤512 px); ≤64 links
- `Accept-Encoding: identity` (sem gzip/br)

## 🗺️ Roadmap

- [ ] Validação de cadeia de certificados (trust store)
- [ ] Verificação criptográfica do CertificateVerify (RSA-PSS/ECDSA)
- [ ] POST/multipart (upload de arquivos)
- [ ] Window-update ACK no TCP (flow control completo)
- [ ] Retomada de sessão (session tickets / PSK)
- [ ] Renderer com layout básico (blocos + negrito/itálico)

## 🐞 Metodologia de debug ("olhos")

Cada camada teve uma versão instrumentada com logs numericos via `sys_debug`
(`[REC]` record • `[HS] handshake • `[KEY]` key schedule • `[HKDF]` • `[TLS_SYS]`),
lidos no **Kernel Live Debug Monitor**. Regras da campanha:
**uma mudança por teste**, snapshot antes de cada patch, revert de 1 arquivo.
As versões `.dbg` de cada módulo estão arquivadas em `debug/` para reativação.

## 📜 Licença

[Escolha: MIT / GPL-3.0 / proprietary] — ver `LICENSE`.
