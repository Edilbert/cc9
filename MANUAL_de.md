# CC9 Handbuch

*English version: [MANUAL.md](MANUAL.md)*

CC9 ist ein ANSI-C-Compiler (C89/C90) für den **Sparrow09**, den
6309-Selbstbaurechner. Er läuft auf jedem Rechner mit einem
Standard-C-Compiler (Linux, macOS, Windows), erzeugt Quelltext für den
Assembler **BS9** und ruft BS9 auf. Das Ergebnis ist eine Sparrow-Datei
(`.x`), die auf dem Sparrow09 mit `LOAD` geladen und mit `RUN` gestartet
wird.

---

## Inhalt

1. [Übersetzen und Installieren](#1-übersetzen-und-installieren)
2. [Aufruf](#2-aufruf)
3. [Programme auf dem Sparrow09 starten](#3-programme-auf-dem-sparrow09-starten)
4. [Speicheraufteilung](#4-speicheraufteilung)
5. [Sprachumfang](#5-sprachumfang)
6. [Datentypen](#6-datentypen)
7. [Gleitkomma](#7-gleitkomma)
8. [Die C-Bibliothek](#8-die-c-bibliothek)
9. [Aufrufkonvention und Assembler](#9-aufrufkonvention-und-assembler)
10. [Programmgröße und Optimierung](#10-programmgröße-und-optimierung)
11. [Testen mit Specht](#11-testen-mit-specht)
12. [Grenzen und bekannte Abweichungen](#12-grenzen-und-bekannte-abweichungen)
13. [Aufbau des Compilers](#13-aufbau-des-compilers)

---

## 1. Übersetzen und Installieren

```
make                      # erzeugt cc9
make install              # nach /usr/local (PREFIX=... ändert das Ziel)
make WIN=y                # Windows-Version mit MinGW
```

`make install` kopiert `cc9` nach `$PREFIX/bin` sowie die Bibliothek
nach `$PREFIX/lib/cc9/lib` und die Header nach `$PREFIX/lib/cc9/include`.

CC9 sucht seine Dateien relativ zum eigenen Programm:

| Was | Gesucht in |
|-----|------------|
| Bibliothek | `<cc9-Verzeichnis>/lib`, sonst `<cc9-Verzeichnis>/../lib/cc9/lib`, oder `-L` |
| Header | `<Bibliothek>/../include`, sonst `<cc9-Verzeichnis>/include` |
| BS9 | `<cc9-Verzeichnis>/bs9`, sonst `bs9` im `PATH`, oder `-B` |

Ohne Installation kann CC9 direkt im Quellverzeichnis benutzt werden.

---

## 2. Aufruf

```
cc9 [Optionen] datei.c ...
```

Alle angegebenen C-Dateien und die C-Bibliothek werden zusammen zu
**einem** Programm übersetzt. Es gibt keinen getrennten Linker: CC9
übersetzt alles in eine BS9-Quelldatei, nimmt dabei nur die Funktionen
und Daten auf, die vom Programm wirklich erreicht werden, und lässt
BS9 die Sparrow-Datei schreiben.

| Option | Bedeutung |
|--------|-----------|
| `-o datei` | Ausgabedatei (Voreinstellung: erste Quelldatei mit `.x`) |
| `-S` | Nur BS9-Quelltext (`.as9`) schreiben, nicht assemblieren |
| `-E` | Nur Präprozessor, Ausgabe auf stdout |
| `-I pfad` | Zusätzliches Include-Verzeichnis |
| `-D name[=wert]` | Makro definieren |
| `-U name` | Makro löschen |
| `-L pfad` | Bibliotheksverzeichnis |
| `-B pfad` | BS9-Programm |
| `-funsigned-char` | `char` ohne Vorzeichen (Voreinstellung: mit Vorzeichen) |
| `-nostdlib` | Ohne C-Bibliothek übersetzen |
| `-O0` | Peephole-Optimierer abschalten |
| `-w` | Keine Warnungen |
| `-k` | `.as9`, `.ls9` und `.sym` nach dem Assemblieren behalten |
| `-v` | Aufruf von BS9 anzeigen |
| `--org=ADR` | Ladeadresse (Voreinstellung `$2000`) |
| `--stack=ADR` | Anfangswert des Stackzeigers (Voreinstellung `$8000`) |
| `--no-header` | Kein Sparrow-Kopf und keine BASIC-Zeile, reine Binärdatei |
| `--version` | Version anzeigen |

Adressen können dezimal, mit `$` oder mit `0x` angegeben werden.

Beispiele:

```
cc9 hello.c                    # -> hello.x
cc9 -o spiel.x main.c grafik.c # mehrere Dateien
cc9 -S -o test.as9 test.c      # Assemblerausgabe ansehen
```

Fehlermeldungen zeigen Datei, Zeile, die Quellzeile und die Stelle:

```
test.c:4: error: expected ';' or ','
       return x;
       ^
```

Ein Aufruf einer Funktion, die nirgends definiert ist, wird vor dem
Assemblieren gemeldet (`undefined reference to 'name'`).

---

## 3. Programme auf dem Sparrow09 starten

Die erzeugte `.x`-Datei hat das Sparrow-Format: 8 Byte Kopf (`S`,
Bank, Ladeadresse, Länge, Prüfsumme 0), danach eine BASIC-Zeile

```
2026 SYS $200F
```

und das Programm. Auf dem Sparrow09:

```
LOAD "HELLO.X"
RUN
```

`RUN` führt die BASIC-Zeile aus und springt mit `SYS` in das
C-Programm. Am Ende von `main()` (oder bei `exit()`) kehrt das
Programm zu BASIC zurück. Die BASIC-Register und der BASIC-Stack
bleiben erhalten.

Mit `--no-header` entsteht eine reine Binärdatei ohne Kopf; der
Einsprung liegt dann an der Ladeadresse.

---

## 4. Speicheraufteilung

```
$0000-$1FFF  gemeinsamer RAM: BIOS, BASIC, Puffer, BASIC-Stack
$1FF8        Sparrow-Kopf (8 Byte, nur in der Datei)
$2000        BASIC-Zeile "2026 SYS $200F"
$200F        Startcode (__start)
             Programmcode
             initialisierte Daten
__eop        Ende der Datei
             nicht initialisierte Daten (werden beim Start gelöscht)
__end        Beginn des Heaps (malloc), wächst nach oben
   ...
$7FFF        Stack, wächst nach unten (Anfang --stack, Voreinstellung $8000)
$8000-$FFFF  ROM
```

Ein Programm hat also **24 KB** für Code, Daten, Heap und Stack. Der
Startcode

1. rettet die BASIC-Register und den BASIC-Stackzeiger,
2. setzt den C-Stack auf `STACK_TOP`,
3. löscht den Bereich der nicht initialisierten Daten,
4. ruft `main(0, NULL)` und danach `exit()` auf.

**BIOS-Aufrufe:** Das BIOS schaltet RAM-Bänke um. Liegt der Stack
während eines BIOS-Aufrufs in Bank 0 (oberhalb von `$2000`), wird er
beim Umschalten unsichtbar. Die Bibliotheksfunktionen schalten deshalb
für jeden `SWI2`-Aufruf auf den gemeinsamen BASIC-Stack um. Wer das
BIOS mit eigenem Assemblercode aufruft, muss das ebenso machen oder
`__bios()` aus `<sparrow.h>` benutzen.

---

## 5. Sprachumfang

CC9 übersetzt den vollen Sprachumfang von ANSI C (C89):

* alle Datentypen einschließlich `float`, `double`, `long double`,
  `struct`, `union`, Bitfelder, `enum`, `typedef`
* Zeiger, mehrdimensionale Felder, Funktionszeiger, komplexe
  Deklaratoren (`int (*f(int))(int, int)`)
* Strukturen als Parameter, Rückgabewert und bei Zuweisungen
* Initialisierer für Felder, Strukturen und Unions, auch geschachtelt
  und unvollständig geklammert, Größe aus dem Initialisierer (`int a[] = ...`)
* `static` und `extern` auf Datei- und Blockebene, mehrere
  Übersetzungseinheiten
* Funktionen mit variabler Parameterzahl (`<stdarg.h>`)
* alte Funktionsdefinitionen (K&R-Stil) und Funktionen ohne Prototyp
  mit den Standard-Argumentumwandlungen
* `switch` mit `int`- und `long`-Werten, `goto`, alle Schleifen
* der vollständige Präprozessor: Makros mit Parametern, `#` und `##`,
  `#if`/`#elif` mit `defined`, `#include`, `#line`, `#error`,
  `#pragma once`, Trigraphen, `__FILE__`, `__LINE__`, `__DATE__`,
  `__TIME__`, `__STDC__`

### Vordefinierte Makros

| Makro | Wert |
|-------|------|
| `__STDC__` | 1 |
| `__CC9__` | 1 |
| `__SPARROW09__`, `__6309__`, `__6809__`, `__BIG_ENDIAN__` | 1 |
| `__CHAR_UNSIGNED__` | 1, nur mit `-funsigned-char` |

### Erweiterungen

* `//`-Kommentare
* `$` in Bezeichnern
* Inline-Assembler: `asm("...")` als Anweisung oder auf Dateiebene
  (siehe [Abschnitt 9](#9-aufrufkonvention-und-assembler))
* `__attribute__((...))` und `__extension__` werden ignoriert
* `#pragma push_macro("name")` / `#pragma pop_macro("name")`
* `long long` wird mit Warnung als `long` (32 Bit) übersetzt
* Aufruf einer nicht deklarierten Funktion: Warnung, Deklaration als
  `int name()` (wie in C89 vorgesehen)

---

## 6. Datentypen

Der 6309 ist eine Big-Endian-CPU. Es gibt keine Ausrichtung: alle Typen
und Strukturelemente liegen auf beliebigen Adressen, Strukturen haben
keine Füllbytes.

| Typ | Größe | Bereich |
|-----|-------|---------|
| `char` | 1 | -128..127 (mit `-funsigned-char`: 0..255) |
| `signed char` / `unsigned char` | 1 | -128..127 / 0..255 |
| `short`, `int` | 2 | -32768..32767 |
| `unsigned short`, `unsigned` | 2 | 0..65535 |
| `long` | 4 | -2147483648..2147483647 |
| `unsigned long` | 4 | 0..4294967295 |
| `float` | 4 | IEEE 754 binary32 |
| `double`, `long double` | 8 | IEEE 754 binary64 |
| Zeiger | 2 | |
| `enum` | 2 | wie `int` |

`size_t` ist `unsigned int`, `ptrdiff_t` und `wchar_t` sind `int`.
`L"..."` erzeugt ein Feld aus 16-Bit-Zeichen.

**Bitfelder** liegen in 16-Bit-Einheiten und werden vom höchstwertigen
Bit an belegt. Ein Bitfeld passt immer in eine Einheit (höchstens 16
Bit); passt es nicht mehr in die angefangene Einheit, beginnt eine neue.
`int`-Bitfelder haben ein Vorzeichen. `enum`-Bitfelder sind
vorzeichenlos, wenn das `enum` keine negativen Werte hat (wie bei GCC).

Division und Rest ganzer Zahlen runden zur Null hin; der Rest hat das
Vorzeichen des Dividenden. `>>` auf negative Werte schiebt arithmetisch.
Eine Division durch 0 löst die Ausnahme der CPU aus (Monitor).

---

## 7. Gleitkomma

`float` und `double` sind IEEE-754-Zahlen. Die Grundrechenarten,
Vergleiche und Umwandlungen sind in Assembler geschrieben
(`lib/float.as9`) und runden genau nach IEEE 754 (round to nearest
even), einschließlich denormalisierter Zahlen, Unendlich und NaN. Die
Ergebnisse sind bitgleich mit denen eines PCs; das wird mit zufälligen
Testvektoren geprüft.

Rechnungen werden in `double`-Genauigkeit ausgeführt (das erlaubt C89),
`float` ist nur ein Speicherformat: Ergebnisse vom Typ `float` werden
beim Zuweisen, bei Umwandlungen und bei `float`-Operationen auf
`float`-Genauigkeit gerundet.

Die Gleitkommaroutinen benutzen feste Arbeitsbereiche im Speicher und
sind deshalb nicht reentrant: In Interrupt-Routinen darf nicht mit
Gleitkommazahlen gerechnet werden.

Die mathematischen Funktionen in `<math.h>` sind in C geschrieben und
auf etwa 15 Stellen genau.

---

## 8. Die C-Bibliothek

Alle Header von C89 sind vorhanden: `assert.h`, `ctype.h`, `errno.h`,
`float.h`, `limits.h`, `locale.h`, `math.h`, `setjmp.h`, `signal.h`,
`stdarg.h`, `stddef.h`, `stdio.h`, `stdlib.h`, `string.h`, `time.h`.
Dazu kommt `sparrow.h` mit Funktionen für den Sparrow09.

### Ein- und Ausgabe

* `stdout` und `stderr` schreiben über die BIOS-Funktion `PUTC` auf die
  Konsole (Bildschirm oder serielle Schnittstelle, je nach Schalter).
  Ein `'\n'` wird vom BIOS als Zeilenwechsel ausgegeben.
* `stdin` liest zeilenweise über `GETC`. Die Eingabe wird angezeigt
  (Echo); Backspace (`$08` oder `$7F`) löscht das letzte Zeichen; mit
  RETURN wird die Zeile an das Programm übergeben. **Ctrl-D** am
  Zeilenanfang bedeutet Dateiende (`EOF`). Mit `__stdin_echo = 0`
  (aus `<sparrow.h>`) wird das Echo abgeschaltet.
* `printf`, `fprintf`, `sprintf`, `vprintf` usw. können alle
  Umwandlungen von C89: `%d %i %u %o %x %X %c %s %p %n %% %e %E %f %g %G`
  mit Flags, Breite, Genauigkeit, `*`, `h`, `l`, `L`.
* `scanf`, `fscanf`, `sscanf` mit `%d %i %u %o %x %c %s %[ %e %f %g %n %p`,
  `*`, Breite und `h`, `l`, `L`.
* Die Ausgabe von Gleitkommazahlen in `printf` und ihr Einlesen in
  `scanf` wird nur dann mitgebunden, wenn das Programm irgendwo mit
  Gleitkommazahlen arbeitet. Ein Programm mit `printf` ist ohne
  Gleitkomma etwa 3,5 KB groß, mit Gleitkomma etwa 9,5 KB.
* Ein Dateisystem gibt es noch nicht: `fopen()` liefert `NULL` mit
  `errno = ENOENT`, `remove()` und `rename()` scheitern.

### Speicher

`malloc`, `calloc`, `realloc` und `free` verwalten den Bereich
zwischen dem Programmende (`__end`) und dem Stack. Zum Stack hin
bleiben mindestens 512 Bytes frei.

### Zeit

`clock()` liefert den 50-Hz-Zähler des BIOS (`CLOCKS_PER_SEC` = 50).
Der Sparrow09 hat noch keine Uhrzeit für C-Programme: `time()` liefert
`(time_t)-1`. `gmtime`, `localtime`, `mktime`, `asctime`, `ctime`,
`strftime` und `difftime` sind vorhanden und rechnen ab 1970.

### Sonstiges

* `exit()` ruft die mit `atexit()` eingetragenen Funktionen auf und
  kehrt zu BASIC zurück; der Rückgabewert steht danach im Register D.
* `abort()` löst `SIGABRT` aus. `signal()` und `raise()` verwalten
  Funktionen für Signale, die mit `raise()` ausgelöst werden.
* `setjmp`/`longjmp` sind in Assembler geschrieben.
* `setlocale()` kennt nur die Umgebung `"C"`.
* `getenv()` liefert `NULL`, `system()` liefert -1.
* `rand()` liefert Werte von 0 bis 32767 (`RAND_MAX`).

### `<sparrow.h>`

| Funktion | Bedeutung |
|----------|-----------|
| `int __bios_putc(int c)` | Zeichen über das BIOS ausgeben |
| `int __bios_getc(void)` | Auf eine Taste warten |
| `int __bios_poll(void)` | Taste lesen, -1 wenn keine gedrückt ist |
| `int __bios(int fn, int a, int x)` | Beliebige BIOS-Funktion (`SWI2`) mit B = `fn`, A = unteres Byte von `a`, X = `x`; liefert D, X danach steht in `__bios_x` |
| `unsigned long __jiffies(void)` | 50-Hz-Zähler |
| `__stdin_echo` | 1: Eingabe anzeigen (Voreinstellung), 0: ohne Echo |
| `PEEK(adr)`, `POKE(adr, wert)` | Speicherzugriff |

---

## 9. Aufrufkonvention und Assembler

### Register

| Register | Verwendung |
|----------|------------|
| U | Rahmenzeiger, wird von jeder Funktion erhalten |
| S | Stack |
| D, W, X, Y | frei, werden nicht erhalten |
| DP | wird vom Compiler nicht benutzt und nicht verändert |

### Parameter und Rückgabewerte

* Parameter werden von rechts nach links auf den Stack gelegt; der
  Aufrufer entfernt sie wieder.
* `char`, `short`, `int`, Zeiger und `enum` belegen 2 Bytes; ein `char`
  steht im unteren (zweiten) Byte.
* `long` belegt 4 Bytes (Big Endian), `double` 8 Bytes.
* `float` belegt 4 Bytes, wenn der Prototyp `float` vorschreibt, sonst
  wird er als `double` (8 Bytes) übergeben.
* Strukturen und Unions werden vollständig auf den Stack kopiert.
* Rückgabe: `char`, `int`, Zeiger in **D** (`char` vorzeichenrichtig
  erweitert), `long` in **Q** (D = höherwertiges Wort, W = niederwertiges).
* Gibt die Funktion `float`, `double` oder eine Struktur zurück, legt
  der Aufrufer zuletzt einen Zeiger auf einen Ergebnispuffer auf den
  Stack. Die Funktion schreibt das Ergebnis dorthin (`float` und
  `double` als 8-Byte-`double`).

### Stackrahmen

```
_func   PSHS  U
        TFR   S,U
        LEAS  -n,S        ; lokale Variablen bei -1,U ... -n,U
        ...
        TFR   U,S
        PULS  U,PC
```

Der erste Parameter liegt bei `4,U`, bei einem Ergebnispuffer bei `6,U`
(der Zeiger auf den Puffer steht bei `4,U`). Funktionen ohne Parameter
und ohne lokale Variablen haben keinen Rahmen.

### Namen

| C | BS9 |
|---|-----|
| globale Funktion oder Variable `name` | `_name` |
| `static` auf Dateiebene | `S<n>_name` (n = Nummer der Datei) |
| `static` in einer Funktion | `S$<n>_name` |
| Laufzeitroutinen | `___name` (z.B. `___lmul`, `___dadd_s`) |

### Inline-Assembler

```c
int twice(int x)
{
   asm("        LDD   4,U\n"
       "        LSLD");          /* Ergebnis in D */
}
```

Der Text wird unverändert in die BS9-Datei übernommen; Zeilen ohne
führende Leerzeichen sind Marken. Auf Dateiebene kann `asm()` ganze
Routinen enthalten. Dort definierte Marken werden nur aufgenommen,
wenn sie vom Programm benutzt werden.

### Laufzeitbibliothek in Assembler

`lib/runtime.as9` (Start, 16/32-Bit-Arithmetik, `setjmp`, BIOS) und
`lib/float.as9` (Gleitkomma) bestehen aus Einheiten, die durch Zeilen
`*=====` getrennt sind. Eine Einheit wird nur gebunden, wenn eine ihrer
Marken benutzt wird. Weitere `.as9`-Dateien im Bibliotheksverzeichnis
werden ebenso behandelt.

---

## 10. Programmgröße und Optimierung

Da nur 24 KB zur Verfügung stehen, achtet CC9 auf die Codegröße:

* **Nur Benutztes wird gebunden:** Ausgehend vom Startcode nimmt CC9
  nur die erreichbaren Funktionen, Daten und Assemblereinheiten auf.
* Die Gleitkommaausgabe in `printf`/`scanf` wird nur bei Bedarf gebunden.
* Konstanten werden schon beim Übersetzen ausgerechnet, kleine
  Ausdrücke direkt mit dem Speicheroperanden verknüpft, Vergleiche von
  `char` mit 8-Bit-Befehlen ausgeführt, `*p++` mit `LEAX` erzeugt.
* Ein Peephole-Optimierer entfernt überflüssige Sprünge, Lade- und
  Vergleichsbefehle und toten Code (`-O0` schaltet ihn ab).
* BS9 kürzt lange Sprünge und `JSR` automatisch.

Richtwerte: `printf("Hello")` ergibt etwa 3,5 KB, ein Programm mit
`printf` und Gleitkommarechnung etwa 9,5 KB; die Gleitkomma-Grundrechenarten
selbst belegen etwa 2 KB.

Mit `-k` bleiben `.as9`, `.ls9` und `.sym` erhalten; das Skript
`tools/sizes.py programm.sym` zeigt die größten Funktionen. Mit
`spechtrun -p` und `tools/profile.py programm.sym` sieht man, in welchen
Funktionen ein Programm seine Zeit verbringt.

---

## 11. Testen mit Specht

Im Verzeichnis `tools/spechtrun` liegt ein Testprogramm, das den
Simulator **Specht** aus dem Sparrow09-Repository unverändert benutzt
(`#include "specht.c"`). Es braucht keine SDL-Bibliothek und kein
Fenster:

1. Es startet das echte Sparrow-ROM, bis BASIC auf eine Taste wartet.
2. Es lädt die `.x`-Datei wie `LOAD`.
3. Es tippt `SYS $xxxx` in den Tastaturpuffer.
4. Alles, was das BIOS auf die Konsole schreibt, geht auf stdout;
   liest das Programm von der Tastatur, kommt die Eingabe von stdin
   (Dateiende als Ctrl-D).
5. Kehrt das Programm zu BASIC zurück, endet `spechtrun` mit dem
   Rückgabewert des Programms (Register B).

```
cd tools/spechtrun
make SPECHT=/pfad/zu/sparrow09/software/specht/specht.c
tools/build_rom.sh /pfad/zu/sparrow09 tests     # sparrow.rom + sparrow.sym
tools/spechtrun/spechtrun -r tests/sparrow.rom -y tests/sparrow.sym hello.x
```

| Option | Bedeutung |
|--------|-----------|
| `-r datei` | ROM (Voreinstellung `sparrow.rom`) |
| `-y datei` | Symboldatei des ROMs (Voreinstellung `sparrow.sym`) |
| `-n zahl` | Höchstzahl ausgeführter Befehle |
| `-v` | Ladeadresse, Einsprung und Befehlszahl anzeigen |
| `-t` | Bei einem Absturz die letzten Befehlsadressen zeigen |
| `-p` | Profil: Anzahl der ausgeführten Befehle je Adresse nach `specht.prof` |

Rückgabewerte: Wert des Programms, 124 = Befehlsgrenze erreicht,
125 = Datei nicht ladbar.

**Hinweis zum ROM:** Die ROM-Quellen lassen sich mit dem aktuellen BS9
nicht assemblieren (siehe [Abschnitt 12](#12-grenzen-und-bekannte-abweichungen)).
`tools/build_rom.sh` arbeitet deshalb auf einer Kopie und passt diese
an; das Ergebnis ist bytegleich mit einem ROM, das mit einer älteren
BS9-Version erzeugt wurde.

### Regressionstests

```
make test                 # tests/run_tests.sh
CTS=/pfad/zu/c-testsuite make test
```

`tests/run_tests.sh` übersetzt die Programme in `tests/c`, lässt sie in
Specht laufen und vergleicht die Ausgabe. Mit Python und numpy werden
zusätzlich zufällige IEEE-Testvektoren für alle Gleitkommaoperationen
erzeugt und geprüft. Mit `CTS` werden die C89-Tests der
[c-testsuite](https://github.com/c-testsuite/c-testsuite) ausgeführt:
164 von 174 bestehen; die übrigen setzen 32-Bit-`int`, Dateien,
`long long` oder C99/C11/GNU-Erweiterungen voraus.

---

## 12. Grenzen und bekannte Abweichungen

* Kein Dateisystem in `stdio` (`fopen` liefert `NULL`).
* `long long` ist 32 Bit breit (C89 kennt den Typ nicht).
* Felder variabler Länge, Anweisungsausdrücke `({...})`, `_Generic`
  und andere C99/C11/GNU-Erweiterungen gibt es nicht.
* Höchstens 255 Argumente je Aufruf.
* BS9 erlaubt 8000 Symbole; ein Programm von etwa 20 KB braucht rund
  1000 Symbole.
* Die Gleitkomma- und Divisionsroutinen sind nicht reentrant (feste
  Arbeitsbereiche).
* `printf` gibt bei mehr als 17 signifikanten Stellen Nullen aus, statt
  die exakte Dezimaldarstellung der Binärzahl zu berechnen.

### Beobachtungen an BS9, ROM und Specht

Beim Entwickeln sind folgende Punkte aufgefallen:

* Das ROM (`software/ROM`) lässt sich mit dem aktuellen BS9 nicht mehr
  assemblieren:
  * `bios.as9` (Zeile 783 ff.) definiert `K_FLDA` … `K_LOAD` als Marken,
    `api.as9` (Zeile 91 ff.) dieselben Namen als Konstanten. Seit
    „Detect duplicate labels“ ist das ein Fehler, obwohl die Werte gleich sind.
  * `FCB` wird mit 16-Bit-Werten benutzt (z.B. `basic.as9` Zeile 569,
    `monitor.as9` Zeile 76). Seit „Align n,PC and FCB with lwasm“ ist
    das ein Fehler; mit `BYTE` entsteht derselbe Code.
* `software/asm/hello.as9` ruft das Makro mit `CALL    PUTS` auf; die
  neue Makrosyntax verlangt `CALL(PUTS)`.
* Specht: `DIVQ` setzt N und Z nach dem niederwertigen Byte von W
  (`Set_Byte_Flags`) und erkennt einen Überlauf des vorzeichenbehafteten
  Quotienten nicht. CC9 wertet diese Flags nicht aus.

---

## 13. Aufbau des Compilers

| Datei | Inhalt |
|-------|--------|
| `main.c` | Aufruf, Optionen, Bibliothek einlesen, BS9 starten |
| `tokenize.c` | Zerlegung in Tokens, Trigraphen, Zeilenfortsetzung |
| `preprocess.c` | Präprozessor (Makroexpansion mit Hidesets) |
| `parse.c` | Parser, Typprüfung, Konstantenauswertung, Initialisierer |
| `type.c` | Typen und übliche arithmetische Umwandlungen |
| `codegen.c` | Codeerzeugung für den 6309, Peephole-Optimierer, Binden |
| `lib/` | C-Bibliothek (C) und Laufzeit (`runtime.as9`, `float.as9`) |
| `include/` | Header |
| `tools/spechtrun` | Testprogramm mit Specht |
| `tests/` | Regressionstests |

Der Aufbau von Tokenizer, Präprozessor und Parser folgt dem Muster von
*chibicc* (Rui Ueyama); Codeerzeugung, Bibliothek und Laufzeit sind
für den Sparrow09 geschrieben. Die Algorithmen der Gleitkommaroutinen
folgen *SoftFloat* (John R. Hauser).

CC9 ist gemeinfrei (Unlicense, siehe `LICENSE`).
