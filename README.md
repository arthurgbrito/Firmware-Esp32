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

Temos também uma Task para controlar a liberação da porta através do sensor de distância, que é `TaskLiberaPorta()`. Ela verifica o valor do Modo Aula local e, quando estiver ativado, monitora pelo sensor de distância se alguém está com a mão próxima. Se estiver, ativa a fechadura por meio da função `magnetizaPorta()` e `desmagnetizaPorta()`.



