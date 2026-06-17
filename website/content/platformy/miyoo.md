---
title: "Miyoo"
weight: 50
params:
  icon: "🕹️"
  familyKey: "handheld"
  asset: "wacki-miyoo.zip"
  sub: "Mini / Mini Plus · OnionOS"
  tagline: "Paczka w standardzie OnionOS Ports — rozpakuj na kartę i graj."
  color: "#14a84a"
  match: ["miyoo", "onion"]
  devices:
    - "Miyoo Mini Plus (platforma referencyjna)"
    - "Miyoo Mini (pin-kompatybilny)"
    - "inne urządzenia z firmware zgodnym z OnionOS Ports"
  requirements:
    - "firmware OnionOS 4.2 lub nowszy (stock nie jest wspierany)"
    - "archiwum wacki-miyoo.zip"
    - "pliki Dane_*.dta z oryginalnej płyty"
  controls:
    - { action: "Ruch kursora", input: "krzyżak (z przyspieszeniem)" }
    - { action: "Kliknięcie lewe", input: "A" }
    - { action: "Kliknięcie prawe", input: "B" }
    - { action: "Menu pauzy", input: "START" }
    - { action: "Quick-save", input: "R1 / R2" }
    - { action: "Quick-load", input: "L1 / L2" }
    - { action: "Wyjście z gry", input: "MENU" }
  logPath: "`Roms/PORTS/Games/Wacki/wacki.log`"
---

## Instalacja

Archiwum `wacki-miyoo.zip` jest zgodne ze standardem OnionOS Ports.

1. Rozpakuj zawartość archiwum bezpośrednio w katalogu głównym karty pamięci.
   Folder `Roms/` z archiwum scali się z istniejącym `Roms/` na karcie.
2. Skopiuj pliki `Dane_*.dta` z oryginalnej płyty do katalogu:
   ```
   Roms/PORTS/Games/Wacki/data/
   ```
3. Włóż kartę, włącz urządzenie i wybierz **Ports → Adventure → Wacki**.

Krzyżak przyspiesza w miarę przytrzymania — krótkie naciśnięcia służą do
precyzyjnego celowania kursorem, dłuższe trzymanie do szybkiego przesuwania go
po ekranie.

<!--tech-->

## Zgodny sprzęt i firmware

Potrzebny jest firmware **OnionOS 4.2** lub nowszy — stock firmware nie jest
wspierany (różni się układ katalogów i mechanizm uruchamiania portów).
Najlepsze wsparcie ma **Miyoo Mini Plus** (platforma referencyjna); **Miyoo
Mini** jest pin-kompatybilny i działa bez zmian. Dla Anbernica i innych
urządzeń z PortMasterem jest [osobna paczka](portmaster.html).

## Coś nie działa?

Wrapper `wacki.sh` zapisuje log do `Roms/PORTS/Games/Wacki/wacki.log` —
dołącz go do zgłoszenia błędu wraz z wersją portu i firmware'u.
