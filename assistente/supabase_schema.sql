-- Leituras enviadas pelas caixas (a cada 5 min).
-- A caixa usa a chave pública "anon": ela só pode INSERIR, nunca ler, alterar ou apagar.
create table if not exists leituras (
  id         bigint generated always as identity primary key,
  caixa      text        not null,           -- identificador da caixa (ex.: CaixaAlerta-1A2B)
  criado_em  timestamptz not null default now(),
  temp       real,                            -- temperatura do cômodo (°C)
  umid       real,                            -- umidade relativa (%)
  presenca   boolean,                         -- sensor de presença
  dcore      real,                            -- aumento estimado da temperatura central (°C), IA na caixa
  suor2h     real,                            -- perda estimada de água em 2 h (g), IA na caixa
  nivel      smallint                         -- 0 normal, 1 atenção, 2 perigo, 3 emergência
);

alter table leituras enable row level security;

create policy so_inserir on leituras
  for insert to anon
  with check (true);

create index if not exists leituras_caixa_tempo on leituras (caixa, criado_em desc);
