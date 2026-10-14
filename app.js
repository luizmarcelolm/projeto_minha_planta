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
})();
