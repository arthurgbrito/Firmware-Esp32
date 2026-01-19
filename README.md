# Resumo organizado do Firmware presente no ESP32 🧑🏻‍💻
Abaixo teremos uma explicação organizada de todo o código que está presente no ESP32. A explicação estará separada por blocos que juntos, executam a tarefa, assim ficando mais fácil de achar e entender. Os blocos serão:

- Carga e descarga da bateria;
- Controle do Modo Aula;
- Controle do Estado da Porta;
- Backup de usuários;
- Cadastro e leitura de crachás;
- Funcionalidades.


### TASKS


Praticamente todos os tópicos possuem uma Task em sua lógica, pois ela é a responsável por fazer rodar todas as aplicações em paralelo através do FreeRTOS (Um Sistema Operacional de Tempo Real embutido no ESP32).
Isso é o que transforma e possibilita todas as aplicações rodarem ao mesmo tempo, cada uma com um tempo pré-estabelecido entre uma execução e outra através de `vTaskDelay()`, apenas um delay no fim da função que dita de quanto em quanto tempo essa Task vai ser executada. Também podemos cofigurar a prioridade de cada Task e qual núcleo do processador irá executar a tarefa.


## Carga e descarga da bateria 🔋


Para esse tópico, a principal função que faz tudo funcionar é a função `TaskControleCarga()`, onde está embutida toda a lógica de controle de carga e descarga da bateria.


<img width="1117" height="255" alt="image" src="https://github.com/user-attachments/assets/2c7efc50-42d1-4417-8dc5-4a0e9e6a8e08" />



Dentro dessa Task temos um switch case que verifica qual o estado atual do ciclo de carga através dos valores do ENUM: COMECAR_CARGA, CARREGANDO e CONCLUIDO_CARGA.
Ao COMECAR_CARGA ele ativa o relé, chaveando o transistor, salva o momento atual em que a carga começa e muda o estado do ciclo para CARREGANDO. 
Quando CARREGANDO, ele verifica se o tempo em que ele está carregando é maior que o tempo limite de carregamento (pré-definido como 10 minutos). No momento em que ele ultrapassar o tempo de um ciclo, o relé será desligado, assim cortando a corrente que vai para a bateria e parando o carregamento. Em seguida é esperado um tempo (também pré-definido) para que a tensão da bateria fique mais estável e assim seja possível medir a tensão com mais precisão. 
Ao realizar a leitura da tensão através do divisor de tensão que está na bateria, é possível estimar a tensão total da bateria. Com a tensão atual do divisor podemos compará-la com os limites máximo e mínimo calculados que essa tensão pode chegar, assim nos permitindo um bom controle do carregamento. Se a tensão do divisor for maior que a tensão máxima calculada, é interrompido o carregamento e o estado do ciclo é alterado para CONCLUIDO_CARGA. Caso contrário, o relé é ativado novamente e o ciclo é reiniciado. Isso acontece até o momento em que a tensão chegue ao valor máximo de carga.
Quando CONCLUIDO_CARGA a tensão é lida em toda a iteração. A partir do momento em que a tensão estiver abaixo do mínimo, o estado do ciclo é alterado para COMECAR_CARGA. 
Para que essa Task tenha o seu funcionamento, é utilizado algumas funções auxiliares, como:

- `readVBat()`: Faz leituras adc e calcula a média dessas medidas para uma melhor precisão. Converte essa tensão digital para decimal e retorna esse valor;
- `ReleOn()` e `ReleOff()`: Faz o chaveamento do transistor, tanto para acionar quanto para desligar o relé;

  
## Controle do Modo Aula 📝

#### Ativação e Desativação do Modo Aula


Para este tópico, a função principal é `atualizaModoAula()`. Ela tem a função de buscar o estado real e atual do Modo Aula diretamente do banco de dados através de uma requisição HTTP que irá retornar o parâmetro "diferente". Se for TRUE, o Modo Aula local precisa ser atualizado com o valor do banco que também será retornado pela requisição. Se for FALSE, o Modo Aula local está igual ao Modo Aula do banco de dados, então está atualizado.
Então vendo de fora, essa função é quem atualiza o Modo Aula fisicamente quando ele é alterado pelo site de Controle de Acesso. Pois utilizamos a lógica para alterar o valor do Modo Aula diretamente no banco de dados independente da forma que foi mudado esse valor (fisicamente ou online). Então se torna mais fácil atualizar e igualar esse valor tanto no ESP quanto no site, pois quando ele é alterado por um crachá (fisicamente) é feita uma requisição HTTP e atualizado no banco de dados. Da mesma forma que quando é alterado pelo site (online), é feito o mesmo processo de quando fisicamente, assim todos recorrem ao banco de dados para consultar o valor certo. 
A função `habilitaModoAula(String cracha)` recebe como parâmetro o crachá que habilitou o Modo Aula, para que faça uma requisição HTTP e envie o crachá e o laborátorio que teve o Modo Aula ativado para uma API que irá atualizar o histórico de entrada do laboratório no site. Após concluir a requisição, é ativado o LED que sinaliza o estado do Modo Aula, é chamada a função `magnetizaPorta()` que irá ativar a fechadura e esperar um tempo, em seguida é desmagnetizada a porta a partir da função `desmagnetizaPorta()`. E para desabilitar o Modo Aula é chamada a função `desabilitaModoAula()` que irá desligar o LED do Modo Aula e desativar a fechadura. 

#### Liberação da porta através do sensor de distância

## Controle do Estado da Porta 🚪


Para captarmos se a porta está fechada ou aberta, utilizamos um sensor magnético *Reed Switch* que é como uma chave que ao aproximar um imã é fechada. No nosso caso, utilizamos o imã na porta e a outra parte do sensor na parede. O *Reed Switch* foi configurado em um pino com Pull Up interno e esse pino era monitorado por uma interrupção capaz de ser criada no ESP32, que ao mudar de valor digital nesse pino, a interrupção é ativada, assim fazendo um controle de uma flag para a leitura do estado da porta e o armazenamento do estado da porta a partir da leitura digital deste pino. 
A função que atualiza o estado da porta no banco de dados é `funcAtualizaEstadoPorta()`, que irá  pegar o valor do estado da porta lida pela interrupção e enviará via requisição HTTP para uma API que se comunicará com o banco, assim atualizando o estado da porta no banco de dados.


## Backup de usuários 📥


Começamos pela Task `TaskAtualizaDBLocal()` que tem o papel de chamar a função `atualizaBackupUsuarios()` em ciclos de tempo determinado. A função `atualizaBackupUsuarios()` faz uma consulta à uma API que irá retornar todos os usuários e suas respectivas tags e irá comparar com os usuários que já estão cadastrados na memória do ESP como backup atual. Se esses JSONs forem diferentes, significa que a memória precisa ser atualizada. Caso sejam iguais, não é necessário nenhuma ação. 
Para ler e alterar os dados da memória do ESP foram feitas duas funções que utilizam a biblioteca LittleFS que nos possibilita editar e gerenciar a memória flash interna. A função `readJson()` é utilizada para ler o conteúdo da memória, enquanto a função `SalvaJson()` sobrescreve o conteúdo anterior pelo novo conteúdo.
`leiaCrachaBackup(tag)` é quem de fato lê os usuários de forma Offline. Ao ser chamada, a função irá ler a memória a partir da função citada acima, criar um objeto que irá receber o conteúdo em JSON que depois será percorrido até achar o usuário com a tag que foi passada como parâmetro da função. Se achar o crachá na memória, retorna *TRUE*, sinalizando que o usuário está permitido de entrar no laboratório. Caso contrário, retorna *FALSE* e o acesso será negado.

## Cadastro e leitura de crachás 🔍 🪪

A Task `TaskNovoRegistro()` é responsável por fazer a requisição à função `verificaNovoRegistro()` de tempos em tempos. Essa função é quem realiza toda a lógica de cadastro do sistema. Essa função está sendo chamada o tempo inteiro e consequentemente está buscando no banco de dados, a partir de uma API, se existe alguém tentando se cadastrar pelo site. Se existir, ele irá esperar alguém aproximar o crachá no leitor RFID do sistema. Ao aproximar, o script junta todas as informações no corpo de uma requisição e envia para uma API que irá atualizar o banco de dados com as novas informações do novo usuário. 


Para a leitura do código do crachá construímos uma função que irá decodificar de fato os caracteres da TAG e retornar o valor em *HEXADECIMAL*. 


Agora, para a leitura frequente dos crachás, a função capaz de fazer isso é `leiaCracha()`. Nesta função, logo ao inicia-la já esperamos o usuário aproximar o crachá. Se não houver aproimação de nenhum crachá, o codigo apenas retorna. Ao aproximar algum crachá ele já é lido a partir da função explicada logo acima e é enviada uma requisição HTTP para uma API com este crachá para a verificação no banco de dados. Se esse crachá estiver liberado (estiver cadastrado no banco de dados), o usuário ativará/desativará o modo aula ou se não estiver liberado (não tiver o cadastro no banco de dados) o acesso será negado.


Isso tudo acontecerá CASO a internet estiver conectada, assim fazendo as consultas diretamente pelas APIs no banco de dados. Caso a internet não estiver conectada, o ESP irá recorrer à sua memória interna onde terá o backup de usuários atualizado e a partir disso será possível autorizar ou negar o acesso.


## Funcionalidades 

Temos algumas funções auxiliares que ajudam a compôr as funcionalidades do sistema:

- `bip(repetições)`: Produz os barulhos de alertas com o buzzer, tendo como parâmetro o número de repetições de 'BIP'(s) a serem executadas;
- `magnetizaPorta()` e `desmagnetizaPorta()`: Liga/Desliga tanto o LED da fechadura quanto a Fechadura Eletromagnética;
- `TaskFrasePrinciapal()`: Responsável por exibir a frase principal no display, mas a exibição é controlada a partir de uma flag que permite desligar a exibição da frase para avisos de uso e funcionalidades.
- `TaskAtualizaDados()`: Responsável por manter em loop as funcionalidades. Então é chamada a função que atualiza o Modo Aula e o estado da porta, em seguida chama a função que lê o crachá, e assim é mantido o loop.


⚠️ OBS: Algumas Tasks possuem apenas uma função sendo chamada dentro dela. Isso pode parecer estranho mas existe um motivo: As funções estão separadas por Tasks para poderem ser executadas em paralelo, assim tendo uma boa eficiência em nosso sistema, pois se juntássemos as funções em uma ou duas Tasks o sistema seria lento e não executaria direito todas as tarefas.
