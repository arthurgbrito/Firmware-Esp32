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



