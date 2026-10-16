# Mac Flash Studio

Build nativo para **Apple Silicon (arm64)** do Flash Studio / Orca-Flashforge, com as novidades do **OrcaSlicer 2.4.2** e várias correções de desempenho para o macOS. Roda como um app separado (nome, ícone vermelho com selo "MAC", bundle id e pasta de dados próprios), então convive com o Flash Studio original.

> Projeto não oficial, sem vínculo com a FlashForge. / Unofficial fork, not affiliated with FlashForge.
> *English summary: native arm64 build of Flash Studio with OrcaSlicer 2.4.2 merged, a Rosetta bridge for FlashForge's x86_64-only network plugin, and macOS performance fixes. Download it from the Releases page.*

## Instalação

Requisitos: Mac com Apple Silicon (M1 ou mais novo) e macOS 11.3+.

1. Baixe `Mac-Flash-Studio-<versão>-arm64.zip` na página de [Releases](../../releases), descompacte e arraste **Mac Flash Studio.app** para `/Applications`.
2. O app é assinado só "ad hoc" (não notarizado). Na primeira abertura, clique com o botão direito → **Abrir**, ou rode:
   ```bash
   xattr -cr "/Applications/Mac Flash Studio.app"
   ```
3. **Plugin de rede:** o download já inclui o plugin da FlashForge (`libFlashNetwork_x64.dylib`, copiado sem modificação do Flash Studio oficial). Ele só existe para x86_64, então precisa do Rosetta: `softwareupdate --install-rosetta`. O plugin é software proprietário da FlashForge, redistribuído aqui apenas para conveniência; os direitos são da FlashForge.
   Se quiser trocar por outra versão do plugin (por exemplo a de um Flash Studio mais novo), use `./install-flashforge-plugin.sh` (também em `tools/mac/`), que copia o plugin do Flash Studio instalado em `/Applications` e reassina o app.

Os dados ficam em `~/Library/Application Support/Mac-Flash-Studio` (o app original continua usando `Orca-Flashforge`).

## O que mudou em relação ao Flash Studio 1.7.15

- **Nativo arm64** e **ponte para o plugin de rede x86_64** (`tools/fnet-bridge`): um *shim* arm64 com a mesma API C repassa as chamadas por um socket para um helper x86_64 (Rosetta) que carrega o plugin original; callbacks, structs e liberação de memória são tratados.
- **Merge do OrcaSlicer 2.4.2** no fork.
- **Interface sem travar em projetos grandes:** o backup automático exportava o projeto inteiro na thread da interface a cada 10 s. Agora usa compressão rápida, sem cursor de espera, e o intervalo padrão é 300 s (migrado uma vez; ajustável em Preferências → Geral).
- **Vista 3D mais fluida:** modelos muito densos (> 120 mil triângulos) são desenhados com uma versão simplificada enquanto a câmera se move (girar/pan/zoom) e voltam ao detalhe total ao parar. Num projeto com ~40 milhões de triângulos: ~15 → ~50 FPS ao girar.
- **Muito menos memória** com vários objetos pintados idênticos: o modelo pintado e a árvore de busca do mouse são compartilhados e a geometria completa não é duplicada. Num projeto com 16 figuras de 2 M de triângulos: ~7,1 → ~2,3 GB (pico 12 → 3,5 GB) e carga de ~22 → ~12 s.
- Fatiamento mais rápido: `mimalloc` como alocador e menos custo no fechamento de pontes (`merge_bridges`).
- Preferências → Gráficos: opção de **Retina** (OpenGL em alta resolução).
- Bloqueio do "modo furtivo" e aviso automático de atualização removidos.

## Observações

- Em pastas protegidas (Documentos, Mesa, Downloads) o macOS pede permissão na primeira vez para o novo app; ao reassinar o app ele pode pedir de novo.
- O modo "realista" com sombras no prato recria a geometria completa dos objetos pintados (usa mais memória).

---

# Orca-Flashforge (original README)
Orca-Flashforge is an open source slicer for FDM printers.

# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2022, CMake, Git, Strawberry Perl.
  - Run `build_release.bat` in `x64 Native Tools Command Prompt for VS 2022`

- Mac 64-bit  
  - Tools needed: Xcode, CMake, Git, gettext, Automake, Perl
  - run `build_release_macos.sh`

# License
Orca-Flashforge is licensed under the GNU Affero General Public License, version 3. Orca-Flashforge is based on Orca Slicer by SoftFever.

Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca-Flashforge includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The flashforge networking plugin is based on non-free libraries from FlashForge. It is optional to the Orca-Flashforge and provides extended functionalities for FlashForge printer users.
