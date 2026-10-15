# Minha Planta · Guia do kit (v1)

Página com quatro abas: **Sobre**, **Montagem**, **Configuração** e **Código**. HTML, CSS e JavaScript puros: sem instalar nada e sem build.

## Arquivos
| Arquivo | O que é |
|---|---|
| `index.html` | Página com todo o conteúdo das abas |
| `style.css` | Visual (claro e escuro automático, celular e desktop) |
| `app.js` | Troca de abas, imagem em tela cheia e carregamento do ESP Web Tools na aba Código |
| `img/` | Imagens em WebP |
| `firmware/` | Código do ESP32 (`.ino`), binário para instalar (`.merged.bin`) e `manifest.json` |
| `favicon.svg` | Ícone da aba do navegador |
| `vercel.json` | Cache das imagens na Vercel |

## Abas e slides

### Sobre
- Projeto Minha Planta: o que mede, o que você pratica e avisos antes de comprar.

### Montagem (4 slides)
1. `n01_conectores.webp` — Encaixe os conectores
2. `n02_solda_por_baixo.webp` — Solde por baixo
3. `n03_modulos_e_cabo.webp` — Encaixe os módulos
4. `n04_tampa.webp` — Feche a tampa

### Configuração (14 slides)
1. `00a_usb_ligado.webp` — Ligue o kit
2. `00b_rede_minha_planta.webp` — Conecte na MINHA_PLANTA
3. `01_wifi_lista_c.webp` — Escolha o seu Wi-Fi
4. `03_wifi_senhas.webp` — Digite as senhas
5. `04_wifi_ok_c.webp` — Pronto!
6. `06_planta_menu.webp` — Escolha a planta
7. `07d_criar_preenchido.webp` — Crie a sua planta
8. `09b_site_planta_inteiro_c.webp` — Acompanhe a planta
9. `08_senha_site.webp` — Senha do site
10. `14_cal_solo_ar.webp` — Calibrar o solo (opcional)
11. `18_cal_temp.webp` — Calibrar a temperatura (opcional)
12. `11_trocar_wifi.webp` — Trocar o Wi-Fi
13. `20_botao_boot.svg` — Sem acesso ao site?
14. Sem imagem — O que o LED quer dizer

### Código
- Baixar o código (`.ino`)
- Restaurar o código original (ESP Web Tools + `manifest.json`)

## Ver no computador
Abra a pasta no VS Code e clique duas vezes no `index.html`. Ou use a extensão **Live Server** (botão "Go Live").

O botão **Instalar código original** só funciona por `https` ou `localhost` (Live Server), no Chrome ou no Edge do computador. Abrindo o `index.html` direto (`file://`), aparece o aviso "Abra o site por https para instalar".

## Trocar o firmware (nova versão do código)
1. Na Arduino IDE, abra o código novo e use **Sketch → Exportar binário compilado**. Na pasta do sketch aparece o arquivo que termina em `.merged.bin`.
2. Na pasta `firmware/`, apague os arquivos da versão antiga e coloque os novos, com o número da versão no nome. Exemplo para a v2:
   - `minha_planta_v2.ino`
   - `minha_planta_v2.merged.bin`
3. No `firmware/manifest.json`, mude o `"version"` e o nome do `.bin`:
   ```json
   {
     "name": "Minha Planta",
     "version": "v2",
     "new_install_prompt_erase": true,
     "builds": [
       { "chipFamily": "ESP32-C3", "parts": [ { "path": "minha_planta_v2.merged.bin", "offset": 0 } ] }
     ]
   }
   ```
4. No `index.html`, na aba Código, troque o link do botão **Baixar código (.ino)**: `href="firmware/minha_planta_v2.ino"`.
5. Envie para o GitHub. A Vercel publica sozinha.

Trocar o número no nome dos arquivos evita que o navegador use uma versão antiga guardada em cache.

## Subir na Vercel
1. Crie um repositório no GitHub e envie esta pasta.
2. Na Vercel: **Add New → Project** → escolha o repositório.
3. Framework Preset: **Other**. Não precisa de Build Command. Clique em **Deploy**.

## Links diretos para mandar ao cliente
- Sobre: `https://SEU-SITE.vercel.app/#sobre`
- Montagem: `https://SEU-SITE.vercel.app/#montagem`
- Configuração: `https://SEU-SITE.vercel.app/#configuracao`
- Código: `https://SEU-SITE.vercel.app/#codigo`
