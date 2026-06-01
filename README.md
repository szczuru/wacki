# Wacki

A portable port of **Wacki** (1997) — a Polish point-and-click
adventure. The game is in Polish, set in Poland, and was made in
Poland; for that reason the rest of this README is in Polish too.

![Wacki](https://i.ytimg.com/vi/pVzsMB6r3hE/maxresdefault.jpg)

## O grze

Wacki to polska przygodówka point-and-click z 1997 roku, której
autorem jest Henryk Cygert. Akcja toczy się w realiach lat
dziewięćdziesiątych w Polsce; sterujesz dwójką bohaterów (Ebek i
Fjej), zbierasz przedmioty, rozmawiasz z napotkanymi postaciami i
rozwiązujesz zagadki, aby przejść kolejne lokacje.

Niniejszy projekt jest portem silnika gry — odtworzonym ze
zdekompilowanej oryginalnej binarki `WACKI.EXE`. Repozytorium
**nie zawiera materiałów z gry**; aby zagrać, potrzebna jest własna
kopia oryginalnej płyty.

## Wymagania

Do uruchomienia gry potrzebne są:

- gotowa binarka portu (sekcja niżej) — dla docelowej platformy
- pliki danych z oryginalnej płyty: `Dane_*.dta`
- oryginalna binarka `WACKI.EXE` z tej samej płyty (wymagana
  jednorazowo w momencie budowania portu; **nie jest potrzebna w
  trakcie gry**, jeśli korzystasz z gotowej binarki z sekcji Releases)

Wspierane platformy:

- macOS (Apple Silicon)
- Linux x86_64
- Windows 10/11 x86_64
- Miyoo Mini Plus oraz kompatybilne handheldy (Anbernic RG35XX,
  Powkiddy RGB30 i pozostałe oparte na SoC SigmaStar SSD20x)

## Uruchomienie

Pobierz gotową binarkę dla swojego systemu z zakładki
[Releases](../../releases). Następnie:

1. Rozpakuj archiwum w dowolnym katalogu.
2. Utwórz obok binarki podkatalog `data/` i skopiuj do niego pliki
   `Dane_*.dta` z oryginalnej płyty.
3. Uruchom binarkę:

   - macOS / Linux: `./wacki`
   - Windows: dwukrotne kliknięcie `wacki.exe`

Gra szuka katalogu z danymi w kolejności: zmienna środowiskowa
`WACKI_PATH`, następnie `./data/`, następnie katalog obok binarki.
Wielkość liter w nazwach plików nie ma znaczenia.

### Miyoo Mini Plus i pokrewne handheldy

Pobierz archiwum `wacki-miyoo.zip` z sekcji Releases. W środku
znajduje się katalog `Wacki.pak/`:

1. Do `Wacki.pak/data/` skopiuj pliki `Dane_*.dta`.
2. Skopiuj cały katalog `Wacki.pak/` na kartę pamięci urządzenia:
   - OnionOS — pod `/Apps/`
   - oryginalne firmware — pod `/App/`
3. Uruchom `Wacki` z menu launchera.

## Sterowanie

| Czynność              | Komputer (mysz + klawiatura) | Handheld (Miyoo)     |
|-----------------------|------------------------------|----------------------|
| Ruch kursora          | mysz                         | krzyżak              |
| Kliknięcie lewe       | LPM                          | przycisk **A**       |
| Kliknięcie prawe      | PPM                          | przycisk **B**       |
| Wyjście z gry         | `ESC`                        | przycisk **Menu**    |
| Quick-save (slot 0)   | `F5`                         | —                    |
| Quick-load (slot 0)   | `F9`                         | —                    |
| Menu pauzy            | `F12`                        | —                    |
| Przełącz postać       | `SPACE`                      | —                    |

Na handheldzie krzyżak przyspiesza w miarę przytrzymania — krótkie
naciśnięcia służą do precyzyjnego pozycjonowania kursora, dłuższe
trzymanie do szybkiego przemieszczania go po ekranie.

## Opcje uruchomienia

Wybrane opcje można podać z linii poleceń lub przez zmienne
środowiskowe:

| Flaga              | Zmienna środowiskowa | Działanie                                        |
|--------------------|----------------------|--------------------------------------------------|
| `--scale N`        | `WACKI_SCALE=N`      | okno powiększone N-krotnie (gra wewnętrznie 640×480) |
| `--scaler MODE`    | `WACKI_SCALER=MODE`  | jakość skalowania: `nearest`, `linear`, `best`   |
| `--seed N`         | `WACKI_SEED=N`       | ustalony seed losowości (do speedrunów / debugu) |
| —                  | `WACKI_PATH=...`     | ścieżka do katalogu z `Dane_*.dta`               |

Przykład: uruchomienie w oknie 1280×960 ze skalowaniem liniowym —

```bash
./wacki --scale 2 --scaler linear
```

## Budowanie ze źródeł

Jeśli nie chcesz korzystać z gotowych binarek, projekt buduje się
standardowym `make`.

Wymagania:

- kompilator C (gcc lub clang)
- biblioteka SDL2 z plikami nagłówkowymi
- `WACKI.EXE` z oryginalnej płyty w katalogu `data/`

Instalacja SDL2:

| System              | Polecenie                                          |
|---------------------|----------------------------------------------------|
| macOS (Homebrew)    | `brew install sdl2`                                |
| Debian / Ubuntu     | `sudo apt install libsdl2-dev`                     |
| Fedora              | `sudo dnf install SDL2-devel`                      |
| Arch                | `sudo pacman -S sdl2`                              |
| Windows (MSYS2)     | `pacman -S mingw-w64-x86_64-{gcc,SDL2} make`       |

Budowanie:

```bash
cp /sciezka/do/plyty/WACKI.EXE data/
cp /sciezka/do/plyty/Dane_*.dta data/

make all
./dist/wacki
```

Wynikowa binarka trafia do `dist/wacki` (lub `dist\wacki.exe` na
Windowsie).

### Wersja dla handheldów

Build cross-kompilowany dla Miyoo Mini Plus odbywa się w kontenerze
Docker (wymagany Docker Desktop lub `docker.io`):

```bash
make miyoo
./tools/pack-miyoo.sh
```

Wynik: `dist/wacki-miyoo.zip` zawierający gotową strukturę `Wacki.pak/`
do przeniesienia na kartę pamięci urządzenia.

## Status portu

Projekt jest na etapie funkcjonalnego portu. Pierwszy rozdział gry
jest grywalny od początku do końca, włącznie z intro, menu,
dialogami, zapisem stanu i wczytaniem. Kolejne rozdziały mają poprawnie
zaimplementowaną logikę wejścia, ale nie zostały zweryfikowane
w pełnym przejściu interaktywnym.

Szczegółowy opis tego, co działa i co jest w trakcie, znajduje się
w katalogu [`docs/`](docs/).

## Licencja i prawa

Port silnika jest dziełem niezależnym i nie zawiera materiałów
chronionych prawem autorskim z oryginalnej gry. Pliki danych
(`Dane_*.dta`, `WACKI.EXE`) pozostają własnością ich twórców i nie
są dystrybuowane wraz z tym repozytorium.

## Podziękowania

- **Henryk Cygert** — autor oryginalnego silnika i projektu gry (1997)
- TopWare Interactive Polska — wydawca oryginalny
