#!/bin/bash
# Gera dois instaladores .pkg a partir dos mesmos plugins universais:
#   <prefixo>-<versao>-macOS-AppleSilicon.pkg      (so arm64)
#   <prefixo>-<versao>-macOS-Intel-macOS10.13.pkg  (so x86_64, macOS 10.13 High Sierra / 10.15 Catalina ou mais recente)
# Uso: mac-split.sh <distribution.xml> <pasta-resources> <prefixo> <versao> <spec> [<spec> ...]
#   spec = "nome-do-componente.pkg|pasta-root|identifier|pasta-scripts|install-location"
#          (pasta-scripts pode ficar vazia; install-location por omissao "/")
# Assinatura: SIGN_ID (Developer ID Application) e INSTALLER_ID (Developer ID Installer) opcionais; sem eles, ad-hoc.
set -euo pipefail
DIST="$1"; RES="$2"; OUT="$3"; VER="$4"; shift 4
# so usa as identidades se estiverem mesmo no porta-chaves (senao assina ad-hoc / instalador sem assinatura)
if [ -n "${SIGN_ID:-}" ] && ! security find-identity -v -p codesigning 2>/dev/null | grep -qF "$SIGN_ID"; then SIGN_ID=""; fi
if [ -n "${INSTALLER_ID:-}" ] && ! security find-identity -v 2>/dev/null | grep -qF "$INSTALLER_ID"; then INSTALLER_ID=""; fi
for ARCH in arm64 x86_64; do
  if [ "$ARCH" = arm64 ]; then TAG=AppleSilicon; else TAG=Intel-macOS10.13; fi
  W="split-$ARCH"; rm -rf "$W"; mkdir -p "$W/pkgs"
  for spec in "$@"; do
    IFS='|' read -r PKG ROOT ID SCR LOC <<< "$spec"; LOC="${LOC:-/}"
    R="$W/root-${PKG%.pkg}"; ditto "$ROOT" "$R"
    # 1) so a arquitetura pedida em todos os binarios Mach-O
    while IFS= read -r -d '' f; do
      a=$(lipo -archs "$f" 2>/dev/null) || continue
      case " $a " in
        *" $ARCH "*) if [ "$a" != "$ARCH" ]; then lipo "$f" -thin "$ARCH" -output "$f.thin"; mv "$f.thin" "$f"; fi;;
        *) echo "::error::$f nao tem $ARCH ($a)"; exit 1;;
      esac
    done < <(find "$R" -type f -print0)
    # 2) voltar a assinar (o lipo invalida a assinatura): bibliotecas soltas primeiro, depois os bundles
    sign () { if [ -n "${SIGN_ID:-}" ]; then codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$1"; else codesign --force --deep --sign - "$1"; fi; }
    while IFS= read -r -d '' f; do sign "$f"; done < <(find "$R" -type f -name "*.dylib" -not -path "*.vst3/*" -not -path "*.component/*" -not -path "*.clap/*" -not -path "*.app/*" -not -path "*.aaxplugin/*" -print0)
    while IFS= read -r -d '' b; do sign "$b"; codesign --verify --deep --strict "$b"; done < <(find "$R" \( -name "*.vst3" -o -name "*.component" -o -name "*.clap" -o -name "*.app" -o -name "*.aaxplugin" \) -prune -print0)
    ARGS=(--root "$R" --identifier "$ID" --version "$VER" --install-location "$LOC")
    [ -n "$SCR" ] && ARGS+=(--scripts "$SCR")
    pkgbuild "${ARGS[@]}" "$W/pkgs/$PKG"
  done
  # 3) distribution: so esta arquitetura e macOS 10.13 ou mais recente
  python3 - "$DIST" "$W/distribution.xml" "$ARCH" <<'PY'
import re, sys
s = open(sys.argv[1], encoding="utf-8").read(); arch = sys.argv[3]
if re.search(r'hostArchitectures="[^"]*"', s): s = re.sub(r'hostArchitectures="[^"]*"', f'hostArchitectures="{arch}"', s)
elif re.search(r'<options\b', s): s = re.sub(r'<options\b', f'<options hostArchitectures="{arch}"', s, count=1)
else: s = s.replace('</installer-gui-script>', f'    <options hostArchitectures="{arch}"/>\n</installer-gui-script>')
if 'allowed-os-versions' not in s:
    s = s.replace('</installer-gui-script>', '    <volume-check><allowed-os-versions><os-version min="10.13"/></allowed-os-versions></volume-check>\n</installer-gui-script>')
open(sys.argv[2], "w", encoding="utf-8").write(s)
PY
  SA=(); [ -n "${INSTALLER_ID:-}" ] && SA=(--sign "$INSTALLER_ID")
  productbuild --distribution "$W/distribution.xml" --resources "$RES" --package-path "$W/pkgs" ${SA[@]+"${SA[@]}"} "$OUT-$VER-macOS-$TAG.pkg"
  echo "== $OUT-$VER-macOS-$TAG.pkg ($(du -h "$OUT-$VER-macOS-$TAG.pkg" | cut -f1))"
done
