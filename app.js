// Minha Planta - Guia do kit (v1): abas, passos em slides e imagem em tela cheia.
// Endereco: #sobre, #montagem, #configuracao, #codigo, ou com o passo: #configuracao-5
(() => {
  const abas = [...document.querySelectorAll('[role=tab]')];
  const paineis = {};
  document.querySelectorAll('[role=tabpanel]').forEach(sec => {
    const trilho = sec.querySelector('.slides');
    if (!trilho) { paineis[sec.id] = { sec }; return; }   // aba sem passos (Sobre)
    const passos = [...trilho.children];
    const ant = sec.querySelector('.ant'), prox = sec.querySelector('.prox');
    const num = sec.querySelector('.cont b'), barra = sec.querySelector('.barra i');
    let atual = 0;
    // mostra o numero do passo, a barra e liga/desliga as setas
    function marca(i) {
      atual = i;
      num.textContent = i + 1;
      barra.style.width = ((i + 1) / passos.length * 100) + '%';
      ant.disabled = i === 0;
      prox.disabled = i === passos.length - 1;
      passos.forEach((p, k) => p.setAttribute('aria-hidden', k !== i));
      const h = '#' + sec.id + (i ? '-' + (i + 1) : '');
      if (!sec.hidden && location.hash !== h) history.replaceState(null, '', h);
    }
    // vai para o passo i (com ou sem animacao)
    function vai(i, suave = true) {
      i = Math.max(0, Math.min(passos.length - 1, i));
      trilho.scrollTo({ left: passos[i].offsetLeft - trilho.offsetLeft, behavior: suave ? 'smooth' : 'instant' });
      marca(i);
    }
    // passo cuja posicao esta mais perto do que aparece na tela
    function maisPerto() {
      const x = trilho.scrollLeft;
      let m = 0;
      passos.forEach((p, k) => { if (Math.abs(p.offsetLeft - trilho.offsetLeft - x) < Math.abs(passos[m].offsetLeft - trilho.offsetLeft - x)) m = k; });
      return m;
    }
    ant.onclick = () => vai(atual - 1);
    prox.onclick = () => vai(atual + 1);
    // ao deslizar com o dedo: descobre qual passo ficou na tela
    let t;
    trilho.addEventListener('scroll', () => {
      clearTimeout(t);
      t = setTimeout(() => marca(maisPerto()), 80);
    }, { passive: true });
    window.addEventListener('resize', () => vai(atual, false));
    paineis[sec.id] = { sec, vai, marca, get atual() { return atual; } };
  });

  // ESP Web Tools: so baixa quando a aba Codigo e aberta
  let espCarregado = false;
  function carregarEsp() {
    if (espCarregado) return;
    espCarregado = true;
    const s = document.createElement('script');
    s.type = 'module';
    s.src = 'https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module';
    document.head.appendChild(s);
  }

  // abas
  function abrir(nome, passo) {
    if (!paineis[nome]) nome = 'sobre';
    abas.forEach(b => {
      const sel = b.dataset.aba === nome;
      b.setAttribute('aria-selected', sel); b.tabIndex = sel ? 0 : -1;
      paineis[b.dataset.aba].sec.hidden = !sel;
    });
    const p = paineis[nome];
    if (nome === 'codigo') carregarEsp();
    if (!p.vai) { if (location.hash !== '#' + nome) history.replaceState(null, '', '#' + nome); return; }
    requestAnimationFrame(() => p.vai(passo == null ? p.atual : passo, false));
  }
  function lerEndereco() {
    const m = location.hash.slice(1).match(/^(sobre|montagem|configuracao|codigo)(?:-(\d+))?$/);
    return m ? [m[1], m[2] ? +m[2] - 1 : 0] : ['sobre', 0];
  }
  abas.forEach(b => b.addEventListener('click', () => abrir(b.dataset.aba)));
  document.addEventListener('click', e => {           // botoes "Começar a montagem" e "Montou? Agora configure"
    const a = e.target.closest('a.proximo');
    if (a) { e.preventDefault(); abrir(a.hash.slice(1), 0); }
  });
  window.addEventListener('hashchange', () => { const [n, i] = lerEndereco(); abrir(n, i); });
  // setas do teclado: troca de passo (ou de aba, quando o foco esta nas abas)
  document.addEventListener('keydown', e => {
    if (e.target.closest && e.target.closest('input, textarea')) return;
    if (document.getElementById('zoom').open || (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft')) return;
    const d = e.key === 'ArrowRight' ? 1 : -1;
    if (abas.includes(document.activeElement)) {
      const n = abas[(abas.indexOf(document.activeElement) + (d > 0 ? 1 : abas.length - 1)) % abas.length];
      n.focus(); abrir(n.dataset.aba); return;
    }
    const p = Object.values(paineis).find(x => !x.sec.hidden);
    if (!p.vai) return;
    e.preventDefault(); p.vai(p.atual + d);
  });
  const [n0, i0] = lerEndereco(); abrir(n0, i0);

  // imagem em tela cheia ao tocar
  const dlg = document.getElementById('zoom'), big = dlg.querySelector('img');
  document.addEventListener('click', e => {
    const b = e.target.closest('.zoom');
    if (!b) return;
    const img = b.querySelector('img');
    big.src = img.currentSrc || img.src; big.alt = img.alt;
    dlg.showModal();
  });
  dlg.addEventListener('click', () => dlg.close());

  // busca: procura em todas as abas, sem acento, sem maiusculas e com sinonimos
  const SIN = {
    internet: ['wi fi', 'rede'], wifi: ['wi fi', 'rede'], rede: ['wi fi'], roteador: ['wi fi', 'boot'], modem: ['wi fi', 'boot'],
    conectar: ['conect', 'wi fi', 'porta'], desconectou: ['wi fi', 'conect'], caiu: ['wi fi', 'conect'], offline: ['wi fi', 'conect'],
    resetar: ['boot', 'rst', 'restaurar', 'erase'], reset: ['boot', 'rst', 'restaurar', 'erase'], reiniciar: ['boot', 'rst'], botao: ['boot', 'rst'],
    restaurar: ['instalar', 'original'], formatar: ['erase', 'apaga', 'instalar'], apagar: ['erase', 'apaga'], excluir: ['apagar'],
    atualizar: ['instalar', 'codigo'], firmware: ['codigo', 'instalar', 'bin'], gravar: ['instalar', 'carregar'], programar: ['codigo', 'arduino'],
    upload: ['carregar', 'instalar'], compilar: ['arduino', 'exportar'], ide: ['arduino'], driver: ['porta', 'arduino'], com: ['porta'],
    pisca: ['piscadas'], piscando: ['pisca'], piscar: ['pisca'], luzinha: ['led'], lampada: ['led'], acende: ['led', 'aceso'], liga: ['led', 'ligar'],
    erro: ['problema', 'defeito', 'failed', 'nao funcionou'], problema: ['defeito', 'erro', 'nao funcionou'], defeito: ['problema'], quebrado: ['defeito'],
    soldar: ['solda'], ferro: ['solda'], estanho: ['solda'], montar: ['montagem', 'encaixe'], caixa: ['case', 'tampa'], case: ['caixa', 'tampa'],
    terra: ['solo'], agua: ['solo', 'umidade'], regar: ['solo', 'umidade da terra'], molhado: ['solo', 'agua'], seco: ['solo'],
    calor: ['temperatura'], frio: ['temperatura'], termometro: ['temperatura'], sol: ['luz', 'dli', 'luminosidade'], claridade: ['luz', 'luminosidade'],
    calibrar: ['calibr'], calibracao: ['calibr'], ajustar: ['calibr', 'ajuste'], precisao: ['calibr'],
    app: ['site', 'navegador'], aplicativo: ['site', 'navegador'], pagina: ['site'], ip: ['endereco', 'minhaplanta local'], endereco: ['ip', 'minhaplanta local'],
    escuro: ['tema'], noturno: ['tema escuro'], cabo: ['usb c'], usb: ['usb c', 'porta'], carregador: ['usb c', 'cabo'],
    cadastrar: ['criar planta'], adicionar: ['criar'], nova: ['criar'], aviso: ['alerta'], alarme: ['alerta'],
    comprar: ['antes de comprar', 'acompanha'], incluso: ['acompanha', 'incluso'], vem: ['acompanha', 'incluso'], ghz: ['2 4 ghz'], esp: ['esp32']
  };
  const PARADAS = new Set(['a', 'o', 'e', 'as', 'os', 'de', 'da', 'do', 'das', 'dos', 'em', 'no', 'na', 'nos', 'nas', 'um', 'uma', 'como', 'que', 'para', 'pra', 'por', 'com', 'meu', 'minha', 'eu', 'se', 'ao', 'qual', 'onde']);
  // mesmo tamanho do texto original: as posicoes servem para destacar a palavra no texto com acento
  const norm = s => s.normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase().replace(/[^a-z0-9]/g, ' ');
  const limpo = (el, fora) => {
    const c = el.cloneNode(true);
    if (fora) c.querySelectorAll(fora).forEach(x => x.remove());
    c.querySelectorAll('td,th,li,p,div,tr').forEach(x => x.append(' '));   // celulas e itens nao grudam no texto
    return c.textContent.replace(/\s+/g, ' ').trim();
  };
  const nomeAba = id => document.querySelector('[data-aba="' + id + '"]').textContent;
  const itens = [];
  function junta(aba, passo, onde, tit, texto, el, extra = '') {
    itens.push({ aba, passo, onde, tit, texto, el, nTit: ' ' + norm(tit), nTudo: ' ' + norm(tit + ' ' + texto + ' ' + extra + ' ' + onde) });
  }
  document.querySelectorAll('.slides').forEach(trilho => {
    const aba = trilho.closest('[role=tabpanel]').id;
    [...trilho.children].forEach((art, k) => {
      const tx = art.querySelector('.texto'), num = tx.querySelector('.num'), grp = tx.querySelector('.grp'), img = art.querySelector('img');
      junta(aba, k, nomeAba(aba) + ' · ' + (num ? num.textContent : k + 1), limpo(tx.querySelector('h3')), limpo(tx, '.grp,.num,h3,.proximo'), art,
        (grp ? grp.textContent : '') + ' ' + (img ? img.alt : ''));
    });
  });
  [['sobre', '.hero', 'h2'], ['sobre', '.card', 'h3'], ['sobre', '.antes', 'h3'],
   ['codigo', '.card', 'h3', '.telas,.plano-b,[slot=unsupported],[slot=not-allowed]'], ['codigo', '.telas', 'h4'], ['codigo', '.plano-b', 'h4']
  ].forEach(([aba, sel, h, fora]) => document.querySelectorAll('#' + aba + ' ' + sel).forEach(el => {
    junta(aba, null, nomeAba(aba), limpo(el.querySelector(h), '[aria-hidden]'), limpo(el, h + ',.proximo' + (fora ? ',' + fora : '')), el);
  }));

  // palavra curta (ip, ar, c3) so vale no comeco de uma palavra, senao acharia "para", "tip"...
  const acha = (n, t) => t.length <= 3 ? n.includes(' ' + t) : n.includes(t);
  function procurar(q) {
    let toks = norm(q).split(' ').filter(Boolean);
    const uteis = toks.filter(t => !PARADAS.has(t));
    if (uteis.length) toks = uteis;
    const grupos = toks.map(t => [t, ...(SIN[t] || [])]);
    const lista = [];
    itens.forEach(it => {
      let pts = 0;
      for (const g of grupos) {
        let m = 0;
        g.forEach((t, j) => {
          const peso = j === 0 ? 1 : .6, inicio = it.nTudo.includes(' ' + t) ? 1.3 : 1;
          if (acha(it.nTit, t)) m = Math.max(m, 10 * peso * inicio);
          else if (acha(it.nTudo, t)) m = Math.max(m, 3 * peso * inicio);
        });
        if (!m) return;
        pts += m;
      }
      lista.push({ it, pts });
    });
    lista.sort((a, b) => b.pts - a.pts);
    return { lista: lista.slice(0, 8).map(x => x.it), termos: [...new Set(grupos.flat())] };
  }
  // trecho do texto em volta da primeira palavra encontrada
  function trecho(raw, termos) {
    const n = norm(raw);
    let pos = -1;
    termos.forEach(t => { const i = n.indexOf(t); if (i >= 0 && (pos < 0 || i < pos)) pos = i; });
    let ini = pos > 45 ? pos - 45 : 0;
    if (ini) { const s = raw.indexOf(' ', ini); if (s > 0 && s < pos) ini = s + 1; }
    return [ini, Math.min(raw.length, ini + 130)];
  }
  function realce(destino, raw, termos, ini = 0, fim = raw.length) {
    const n = norm(raw), marcas = [];
    termos.forEach(t => {
      for (let i = n.indexOf(t, ini); i >= 0 && i < fim; i = n.indexOf(t, i + t.length)) {
        if (t.length <= 3 && i > 0 && n[i - 1] !== ' ') continue;
        marcas.push([i, Math.min(fim, i + t.length)]);
      }
    });
    marcas.sort((a, b) => a[0] - b[0]);
    let p = ini;
    if (ini > 0) destino.append('…');
    marcas.forEach(([a, b]) => {
      if (b <= p) return;
      a = Math.max(a, p);
      destino.append(raw.slice(p, a));
      const m = document.createElement('mark'); m.textContent = raw.slice(a, b); destino.append(m);
      p = b;
    });
    destino.append(raw.slice(p, fim));
    if (fim < raw.length) destino.append('…');
  }

  const busca = document.querySelector('.busca'), campo = document.getElementById('busca'), res = document.getElementById('busca-res');
  let achados = [], ativo = -1;
  function escondeLista() { res.hidden = true; campo.setAttribute('aria-expanded', 'false'); }
  function fechaBusca(limpar) {
    escondeLista();
    busca.classList.remove('aberta');
    if (limpar) { campo.value = ''; busca.classList.remove('tem-texto'); }
  }
  function mostra() {
    const q = campo.value.trim();
    busca.classList.toggle('tem-texto', !!q);
    res.textContent = ''; ativo = -1; campo.removeAttribute('aria-activedescendant');
    if (!q) { escondeLista(); return; }
    const { lista, termos } = procurar(q);
    achados = lista;
    if (!lista.length) {
      const li = document.createElement('li'); li.className = 'br-vazio';
      li.append('Nada encontrado para "' + q + '". Tente:');
      const sug = document.createElement('div'); sug.className = 'br-sug';
      ['Wi-Fi', 'LED', 'calibrar', 'BOOT', 'instalar'].forEach(s => {
        const b = document.createElement('button'); b.type = 'button'; b.textContent = s;
        b.addEventListener('mousedown', e => e.preventDefault());
        b.addEventListener('click', () => { campo.value = s; mostra(); campo.focus(); });
        sug.append(b);
      });
      li.append(sug); res.append(li);
    }
    lista.forEach((it, k) => {
      const li = document.createElement('li'); li.id = 'br-' + k; li.setAttribute('role', 'option'); li.setAttribute('aria-selected', 'false');
      const onde = document.createElement('span'); onde.className = 'br-onde'; onde.textContent = it.onde;
      const tit = document.createElement('span'); tit.className = 'br-tit'; realce(tit, it.tit, termos);
      const tx = document.createElement('span'); tx.className = 'br-txt'; realce(tx, it.texto, termos, ...trecho(it.texto, termos));
      li.append(onde, tit, tx);
      li.addEventListener('mousedown', e => e.preventDefault());
      li.addEventListener('click', () => ir(it));
      res.append(li);
    });
    res.hidden = false; res.scrollTop = 0; campo.setAttribute('aria-expanded', 'true');
  }
  // abre a aba (e o passo) do resultado e pisca o lugar encontrado
  function ir(it) {
    fechaBusca(true);
    campo.blur();
    abrir(it.aba, it.passo);
    setTimeout(() => {
      if (it.passo == null) it.el.scrollIntoView({ behavior: 'smooth', block: 'center' });
      else window.scrollTo({ top: 0, behavior: 'smooth' });
      it.el.classList.remove('achado'); void it.el.offsetWidth; it.el.classList.add('achado');
    }, 60);
  }
  campo.addEventListener('input', mostra);
  campo.addEventListener('focus', () => { if (campo.value.trim()) mostra(); });
  campo.addEventListener('keydown', e => {
    const ops = [...res.querySelectorAll('[role=option]')];
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      if (!ops.length) return;
      e.preventDefault();
      const d = e.key === 'ArrowDown' ? 1 : -1;
      ativo = ativo < 0 ? (d > 0 ? 0 : ops.length - 1) : (ativo + d + ops.length) % ops.length;
      ops.forEach((o, k) => o.setAttribute('aria-selected', k === ativo));
      ops[ativo].scrollIntoView({ block: 'nearest' });
      campo.setAttribute('aria-activedescendant', ops[ativo].id);
    } else if (e.key === 'Enter') {
      e.preventDefault();
      if (achados.length) ir(achados[Math.max(0, ativo)]);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      if (campo.value) { campo.value = ''; mostra(); } else { fechaBusca(true); campo.blur(); }
    }
  });
  busca.querySelector('.busca-abrir').addEventListener('click', () => { busca.classList.add('aberta'); campo.focus(); });
  busca.querySelector('.busca-fechar').addEventListener('click', () => {
    if (busca.classList.contains('aberta')) fechaBusca(true);
    else { campo.value = ''; mostra(); campo.focus(); }
  });
  document.addEventListener('click', e => { if (!busca.contains(e.target)) fechaBusca(false); });
  // tecla "/" vai direto para a busca
  document.addEventListener('keydown', e => {
    if (e.key !== '/' || dlg.open || (e.target.closest && e.target.closest('input, textarea'))) return;
    e.preventDefault(); busca.classList.add('aberta'); campo.focus();
  });
})();
