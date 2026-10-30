# Regras de segurança do assistente

1. **Não diagnostica.** Sinais de alarme (confusão, desmaio, pele quente e seca, vômito, falta de ar) levam direto à orientação de ligar para o **SAMU 192**, antes de qualquer outra resposta. Essa checagem é uma regra fixa no código, feita antes de chamar o modelo de linguagem.
2. **Não inventa lugares.** Os locais climatizados vêm de uma lista verificada (levantada pela equipe, pela prefeitura ou pelo OpenStreetMap). O modelo só escolhe entre eles e explica.
3. **Respeita o perfil.** Para idoso acamado, não sugere sair de casa: foca em água, ventilação, roupas leves e troca de cômodo.
4. **Ventilador com limite.** Acima de 37–38 °C, o ventilador sozinho deixa de ajudar idosos (Morris et al., 2021, *The Lancet Planetary Health*). Nessa faixa o assistente não o recomenda como solução e insiste em outras medidas e no alerta à família ou ao cuidador.
5. **LGPD.** Dados de saúde são sensíveis: consentimento explícito, guardar só o necessário, apagar quando pedido e nunca usar para treinar modelos.
6. **Não substitui o alerta.** O alerta principal continua na caixa e funciona sem internet.
