# Capturas de tela da documentação (docs/img/telas). Para gerar de novo:
#   sh build.sh test tests/readme.txt --out docs/img/telas
sleep 16
shot bloqueio
key ret
sleep 3
shot area-de-trabalho
# Explorador à esquerda, Terminal à direita e a Calculadora por cima
key alt-f4
sleep 1
key meta_l-e
sleep 2.5
key meta_l-left
sleep 1
key meta_l-t
sleep 2.5
key meta_l-right
sleep 1
type sysinfo
key ret
sleep 1.5
key meta_l-spc
sleep 1
type calc
sleep 1
key ret
sleep 2
type 12*3=
sleep 1
shot aplicativos
key meta_l
sleep 2
shot launchpad
key esc
sleep 1
key meta_l-tab
sleep 2
shot mission-control
key esc
sleep 1
# tema escuro, pelo Terminal
key alt-tab
sleep 1
type tema escuro
key ret
sleep 2
shot modo-escuro
