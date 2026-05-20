Gabriel Tortolio Fonseca — 10416751
Ambiente Experimental
● Plataforma: GitHub Codespaces
● Compilador: GCC com flags -O2 -fopenmp -Wall
● Threads testadas: 1, 2, 4, 8
● Datasets: log_distribuido.txt (10M linhas, acesso uniforme) e log_concorrente.txt
(10M linhas, ~90% dos acessos concentrados em poucas URLs)
● Observação: O ambiente de execução em container impossibilitou o uso de perf
(sem acesso a contadores de hardware) e limitou o uso do Valgrind/Cachegrind
(overflow de heap durante simulação com 10M de linhas). As medições afetadas são
reportadas com a devida justificativa.
Experimento A: Escalabilidade
Threads Tempo de Execução
(s)
Speedu
p
1 (seq) 7,263 1,000
2 5,722 1,269
4 5,709 1,272
8 5,700 1,274
Análise
O speedup observado é muito baixo para um dataset de 10 milhões de linhas em uma
máquina com supostamente 8 núcleos. Isso indica que o ganho real de paralelismo é quase
inexistente a partir de 2 threads.
A fase de leitura do arquivo é sequencial. Se o log é carregado linha a linha sem
pré-alocação eficiente, o tempo de leitura domina o tempo total, tornando a paralelização da
fase de contagem pouco impactante.
Experimento B: Contenção
Crescimento do System Time e Overhead do SO
O System Time de 1,85 s representa aproximadamente 30,7% do tempo total de CPU, o
que é desproporcionalmente alto.
Esse overhead elevado é consequência direta do comportamento da diretiva #pragma omp
critical: quando uma thread entra na região crítica, todas as demais são bloqueadas e ficam
em espera ou são suspensas pelo SO. A troca de contexto entre threads suspensas e
acordadas gera chamadas de sistema frequentes, elevando o tempo em modo kernel.
Impacto da Sincronização Excessiva
A sincronização excessiva causada pela diretiva #pragma omp critical serializa a execução
e força as threads a passarem grande parte do tempo ociosas competindo por locks, o que
resulta em um tempo real de execução superior ao tempo total de processamento e em uma
grave subutilização do sistema, evidenciada pelo uso ineficiente de apenas 80% da CPU.
IPC (Instructions Per Cycle)
A medição de IPC via perf não foi possível no ambiente Codespaces, pois o container
Docker utilizado pela plataforma bloqueia o acesso aos contadores de hardware do
processador por restrições de segurança. O erro retornado confirma isso:
Estimativa: Esperaria-se um IPC baixo na versão critica, pois as threads passam grande
parte do tempo paradas aguardando o lock. Na versão atomic, onde o lock é mais fino, o
IPC tende a ser significativamente maior, pois as threads ficam ociosas por menos tempo.
Experimento C: False Sharing
Por que o False Sharing ocorre na versão sem padding?
Quando múltiplas threads atualizam hit_count de nós distintos que residem na mesma linha
de cache, o protocolo de coerência de cache invalida a linha inteira sempre que qualquer
thread escreve. Isso força as demais threads a recarregar a linha da memória, mesmo que
o campo que elas querem atualizar não tenha sido modificado.
Medição
A medição via perf e Valgrind/Cachegrind não foi possível no ambiente Codespaces pelos
motivos já descritos. O Cachegrind especificamente apresentou overflow de heap mesmo
com subconjuntos reduzidos do dataset (300.000 e 5.000 linhas), tornando inviável qualquer
medição simulada.
Resultado qualitativo esperado: Com padding, o número de cache misses deveria
diminuir significativamente no dataset concorrente, onde múltiplas threads competem pelos
mesmos buckets. No dataset distribuído, o impacto é menor, pois as threads tendem a
acessar regiões distintas da memória naturalmente.
Questões de Análise
Q1: Por que a versão critical pode degradar sob alta contenção?
O #pragma omp critical utiliza um único lock global para toda a tabela hash. Com alta
contenção, todas as threads disputam o mesmo lock, resultando em execução
essencialmente sequencial dentro das regiões críticas ou seja mais overhead de
sincronização do que a versão sequencial pura, sem o benefício do paralelismo.
Q2: Como locks por bucket equilibram sincronização e paralelismo?
Em vez de um lock único, cada bucket possui seu próprio omp_lock_t. Duas threads só
bloqueiam uma se tentarem atualizar URLs no mesmo bucket simultaneamente. Com
131.071 buckets e distribuição razoável, a probabilidade de conflito é baixa para o dataset
distribuído. No dataset concorrente, os hotspots concentram-se em poucos buckets, e a
contenção persiste nesses buckets específicos, mas o restante da tabela permanece
acessível sem bloqueio.
Q3: Compare cache misses entre versões atomic e padded.
Sem padding, nós adjacentes compartilham linhas de cache. Quando uma thread atualiza
hit_count de um nó, invalida a linha de cache para todas as threads que possuem outros
nós nessa linha, mesmo que esses nós não sejam o alvo da atualização. Com padding
alinhado a 64 bytes, cada nó ocupa exatamente uma linha de cache, eliminando esse efeito.
Q4: Houve speedup superlinear?
Não. O speedup máximo observado foi de 1,274 com 8 threads, muito abaixo do linear ideal
(8,0). O speedup superlinear costuma ocorrer quando a porção de dados de cada thread
cabe inteiramente no cache de alta velocidade (L1/L2) de cada núcleo. Isso não aconteceu
neste projeto porque a tabela hash é maior que a capacidade desses caches e,
principalmente, porque a alta contenção gerada pelas diretivas de sincronização (critical,
atomic ou locks) na mesma estrutura compartilhada forçou as threads a esperarem umas
pelas outras, inviabilizando o ganho de escala.
Q5: Quando o aumento de memória causado pelo padding torna-se vantajoso?
O padding é vantajoso quando:
● O número de threads concorrentes é alto (≥ 4);
● Múltiplas threads escrevem frequentemente em nós próximos na memória;
Torna-se desvantajoso quando a estrutura é muito grande e não cabe mais nos caches,
gerando mais cache misses por capacidade do que economiza por coerência.
Q6: Qual solução seria utilizada em uma CDN real?
Em uma CDN real, a abordagem mais adequada seria uma combinação de estratégias:
● Sharding por thread: cada thread mantém sua própria cópia parcial dos contadores
(estruturas locais), e os resultados são agregados ao final — eliminando
sincronização durante o processamento;
● Estruturas lock-free: tabelas hash com operações CAS (Compare-And-Swap) sem
locks explícitos;
● Aproximação: contadores para estimativas probabilísticas de alta frequência,
reduzindo a necessidade de sincronização exata.
O bucket locking é uma boa solução intermediária para sistemas de médio porte,
equilibrando corretude, complexidade de implementação e desempenho.
Q7: Como listas encadeadas impactam localidade de cache?
Listas encadeadas têm localidade de cache ruim: cada nó é alocado dinamicamente em
posições arbitrárias da heap, forçando acessos não sequenciais à memória durante a
travessia. Em uma tabela hash com encadeamento, encontrar um elemento requer seguir
ponteiros dispersos, causando frequentes cache misses de dados.
Conclusão
Os experimentos demonstraram que a escolha da estratégia de sincronização tem impacto
direto e mensurável no desempenho de aplicações paralelas. A versão critical apresentou o
pior desempenho sob alta contenção, com overhead de sistema significativo. A versão com
bucket locks oferece melhor escalabilidade ao reduzir a contenção global. O false sharing é
um problema real que pode ser mitigado com padding, ao custo de maior consumo de
memória.
As limitações do ambiente de execução impediram a coleta de algumas métricas
quantitativas.
