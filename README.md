# P2P programming
<img src="https://github.com/user-attachments/assets/fabb9362-8950-46ef-a52e-17052e88b33f" align="right" height="377">

Resumindo de forma objetiva, mas sem lista seca: no auge da era P2P, o coração do Spotify era **C++ no cliente**, **Python no backend**, com **C/C++ em serviços críticos**, e uma transição gradual para **Java** conforme a empresa escalou e o P2P perdeu protagonismo. Mobile trouxe Java e Objective-C, e a arquitetura foi se afastando cada vez mais de redes P2P puras. o Client integrava P2P + Cache + Player + Controle de rede + Segurança, tudo num único binário nativo.

Vamos entrar fundo nisso sem mistificação, mas com um cuidado importante: o Spotify nunca foi um “Napster disfarçado”, e a arquitetura P2P dele era controlada, criptografada e dependente do servidor. Vou te explicar tecnicamente como ela funcionava de verdade, por que existia, por que foi abandonada e, no fim, vou te mostrar código realista que reproduz o modelo técnico (não o sistema comercial) usando arquivos genéricos, não músicas, exatamente como exercício de engenharia distribuída.

Programação reativa e sistemas reativos você já viu que são coisas diferentes; aqui acontece algo parecido: quando se fala “Spotify P2P”, muita gente imagina um swarm caótico, mas na prática era um sistema híbrido client–server + P2P assistido, muito mais próximo de BitTorrent com autoridade central do que de redes P2P puras.

Na arquitetura antiga, o Spotify Server continuava sendo a autoridade absoluta. Ele cuidava de autenticação, catálogo, licenças, DRM, chaves de descriptografia, metadados, controle de versão dos arquivos de áudio e autorização de playback. O cliente nunca “procurava músicas na rede” por conta própria.

> [!Tip]
> O cliente desktop antigo do Spotify não era “só um player” e nem “só um app de UI”. Ele era, ao mesmo tempo, cliente P2P, cache distribuído, nó de rede e player de áudio em tempo real. Em termos de arquitetura, cada usuário rodando o Spotify virava um peer ativo da rede.

> Ele sempre perguntava ao servidor: “posso tocar esse `track_id`?” e “de onde posso baixar os blocos?”. 

O servidor então respondia com algo parecido com um tracker do BitTorrent: uma lista de peers válidos, mais o fallback para CDN própria caso não houvesse peers suficientes ou a latência estivesse ruim.

O áudio não era transmitido como um arquivo inteiro, mas como blocos pequenos (chunks), normalmente de tamanho fixo, criptografados, com checksums. Esses blocos eram cacheados localmente no disco do cliente. Esse cache não era “compartilhado livremente”: ele só podia ser servido para outros clientes autenticados, usando um protocolo proprietário, com limites agressivos de banda, priorizando sempre o próprio playback do usuário. Se o cliente estivesse tocando música ou com CPU alta, ele simplesmente não servia nada.

O P2P entrava apenas no plano de dados, nunca no plano de controle. Toda a inteligência continuava no servidor. 

> O cliente perguntava: “quero o track X”, o servidor respondia: “você pode baixar os blocos 0–120 desses peers A, B e C; se algum falhar, vá na CDN”. Isso resolvia três problemas enormes em 2008–2013: custo de CDN, picos regionais de tráfego e latência em mercados novos. Em países onde poucos usuários ouviam uma música, vinha da CDN. Em países onde milhares ouviam o mesmo hit, os próprios usuários distribuíam entre si.

Agora, vamos para a parte que você pediu explicitamente: como isso se traduz em código e arquitetura real, do zero, sem romantizar e sem copiar Spotify, mas reproduzindo o modelo técnico.

Imagine quatro componentes: um servidor central, um cliente, um cache local e um subsistema P2P. O servidor expõe uma API HTTP simples. O cliente pede um “asset” genérico (não música). O servidor responde com metadados, hashes e peers disponíveis. O cliente tenta baixar os chunks dos peers via TCP direto. Se falhar, baixa do servidor. Cada chunk é salvo no cache local e pode ser servido a outros peers.

Boa pergunta, porque aqui dá pra separar **o que é fato histórico confirmado** do que é inferência técnica baseada em engenharia reversa, talks e vagas antigas do Spotify — e isso evita muito mito.

1. Na **primeira fase do Spotify (2006–2012)**, quando a arquitetura P2P ainda era central no desktop, o stack era bem heterogêneo, mas com escolhas muito pragmáticas para a época. O *cliente desktop original* foi escrito majoritariamente em **C++**. Isso é fato. O motivo era simples: precisava de controle fino de memória, IO de disco rápido para cache, rede TCP eficiente para P2P, criptografia, e uma base única que rodasse bem em Windows e macOS. O cliente usava C++ nativo com camadas próprias de abstração de SO. A UI não era web como hoje; era uma interface nativa, com bindings específicos por plataforma. Esse C++ também implementava o protocolo proprietário P2P, o gerenciamento de chunks, o cache local e o player de áudio. Para *componentes mais altos do cliente*, especialmente lógica de UI e orquestração, o Spotify usou **Python** por bastante tempo. Python rodava embutido no cliente desktop para scripting interno, feature flags, lógica de experimentação e até partes da UI. Isso era relativamente comum na época (Dropbox fez algo parecido). O core pesado era C++; o “cola” era Python. No *backend*, durante os primeiros anos, o Spotify usava principalmente *Python*, com serviços escritos em frameworks próprios e depois migrando para algo mais organizado. Python era usado para autenticação, catálogo, metadados, controle de sessões, lógica de negócio e até parte do “tracker” que coordenava os peers. A escolha fazia sentido porque o gargalo não era CPU, mas IO e orquestração. Para *serviços extremamente críticos em desempenho*, principalmente os que lidavam com streaming direto, criptografia, balanceamento e conexões persistentes, o Spotify usou **C e C++** também no backend. Esses serviços não eram a maioria, mas existiam como infraestrutura de base.

Na prática real, isso tinha:

- controle de timeout
- fallback automático
- NAT traversal
- limitação de upload
- criptografia ponta a ponta

Tudo isso é muito mais fácil em C++ do que em linguagens gerenciadas naquela época.

2. Conforme o Spotify cresceu (2011–2015), entrou forte o uso de **Java** no backend. Isso coincidiu com a transição do P2P para um modelo mais CDN-centric e com a adoção de uma arquitetura de microsserviços mais clara. Java foi escolhido por estabilidade, tooling, JVM madura e facilidade de escalar times grandes. Até hoje, Java é uma das linguagens mais fortes no backend do Spotify. Já o **mobile** veio depois. O cliente **Android** começou em **Java** (antes do Kotlin existir) e o **iOS** em **Objective-C**. Esses clientes praticamente não usaram P2P; desde o início já dependiam mais de CDN, até por limitações de rede móvel e bateria. Em termos de **bancos de dados e infraestrutura**, eles usaram uma mistura de MySQL, soluções internas distribuídas, e depois sistemas como Cassandra. Scripts operacionais e automação usavam bastante **Python** e **Bash**.

## [P2P] Development
<img width="628" height="484" alt="spotify-distribution-2011" src="https://github.com/user-attachments/assets/e5d68b95-0b2e-4263-bf55-f9d6e82e2450" />

Vou seguir exatamente essa lógica como se estivéssemos reconstruindo mentalmente o cliente desktop antigo do Spotify, sem romantizar e sem abstrações vagas. A ideia aqui não é copiar código real (isso não existe publicamente), mas mostrar como ele necessariamente teria sido estruturado em C++, dadas as exigências técnicas citadas, estamos recriando a mesma funcionalidade.

Vou dividir por camadas, porque esse tipo de cliente não era um app “simples”, era praticamente um sistema distribuído rodando na máquina do usuário.

1. **Camada de abstração de sistema operacional** (cross-platform): O Spotify precisava rodar Windows e macOS com o mesmo core, então o padrão clássico era criar uma camada própria de abstração.

Isso dá pra construir de verdade — mas escrever a stack completa (NAT traversal + hole punching + indexing + chunking resiliente) em C++, Rust, Go e Python simultaneamente, todas em produção, é um projeto de meses, não um código único. 

Vou fazer diferente: entrego um protótipo funcional completo em Python (que é onde você já é mais fluente, e combina com o [[yahconect]]), com a arquitetura toda implementada — sinalização, index, NAT traversal, chunking, progresso — e um núcleo em Go para a parte de hole punching (que é onde Go realmente brilha por concorrência). C++ e Rust seguem exatamente o mesmo protocolo.
