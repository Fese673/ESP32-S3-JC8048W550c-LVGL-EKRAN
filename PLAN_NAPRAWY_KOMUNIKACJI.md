# Plan naprawy komunikacji i synchronizacji

Dotyczy projektów:
- [ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main](ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main)
- [ZEGAR-ESP32](ZEGAR-ESP32)

## 1. Co jest teraz nie tak

### ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main
- Komunikacja jest zrobiona jako prosty framing UART w `CommunicationProtocol.cpp`.
- Ramki mają start `0xAA`, typ, sekwencję, długość i CRC16, ale nie ma prawdziwej sesji ani stanu „peer ready”.
- Inicjalizacja komunikacji startuje dopiero podczas startu UI w `main/app/main.cpp`, więc pierwsze ramki mogą polecieć zanim druga strona będzie gotowa.
- `updateUi()` tylko wpycha ostatni snapshot i ustawienia do EEZ, ale nie wymusza wspólnego handshake'u ani ponownej synchronizacji po starcie.

### ZEGAR-ESP32
- Ten projekt ma dojrzalszą warstwę usługową: WiFi sync, MQTT sync, RTC sync, orchestrator i UART bridge do Guition.
- Mimo tego nadal nie ma trwałego, wspólnego protokołu boot handshake między dwoma urządzeniami.
- Po starcie jedna strona może ruszyć szybciej, wysłać dane, a druga to po prostu przegapi, bo obecnie dominuje model okresowych broadcastów i odpytywania.

## 2. Najbardziej prawdopodobne źródła problemów

1. Brak jednego uzgodnionego momentu „obie strony są gotowe”.
2. Brak numeru bootu / identyfikatora sesji, więc nie da się odróżnić świeżego startu od starych ramek.
3. Brak obowiązkowego potwierdzenia pierwszego pełnego syncu.
4. Zbyt późny start komunikacji po jednej stronie i zbyt wczesne wysyłki po drugiej.
5. Brak mechanizmu ponawiania pierwszej synchronizacji aż do skutku.

## 3. Docelowy model

### Zasada nadrzędna
Synchronizacja ma być odporna na to, która strona wstanie wcześniej. Każde urządzenie ma:
- lokalnie uruchomić odbiór jak najwcześniej,
- ogłosić gotowość po starcie,
- wysłać pełny stan startowy dopiero po wykryciu gotowości peer'a,
- ponawiać handshake aż do otrzymania potwierdzenia.

### Minimalny zestaw pól protokołu
- `protocolVersion`
- `deviceRole`
- `bootId` albo `sessionId`
- `uptimeMs`
- `stateAgeMs`
- `capabilities`
- `firstSyncDone`

### Minimalne stany
- `BOOTING`
- `UART_READY`
- `PEER_DETECTED`
- `FIRST_SYNC_SENT`
- `FIRST_SYNC_CONFIRMED`
- `STEADY_STATE`

## 4. Plan naprawy

### Etap 1: Uporządkować odpowiedzialności
- Rozdzielić logikę transportu od logiki stanu urządzenia.
- Zostawić UART tylko jako warstwę ramek.
- Przenieść decyzje o tym, kiedy wysłać pełny stan, do jednego kontrolera synchronizacji.

### Etap 2: Dodać handshake startowy
- Po uruchomieniu każda strona wysyła `HELLO` z `bootId`, wersją protokołu i podstawowymi zdolnościami.
- Druga strona odpowiada `HELLO_ACK` z własnym `bootId` i aktualnym stanem.
- Dopiero po `HELLO_ACK` strony uznają, że peer jest gotowy.

### Etap 3: Wymusić wczesny odbiór
- Uruchomić odbiór UART jak najwcześniej w boot flow.
- Nie czekać na pełny UI do pierwszego handshaku.
- Pierwszy sync ma być możliwy jeszcze zanim interfejs jest w pełni gotowy.

### Etap 4: Zrobić deterministyczny pierwszy sync
- Po `HELLO_ACK` wysłać pełny snapshot stanu.
- Jeśli peer nie odpowiada, powtarzać wysyłkę po krótkim backoffie.
- Po pierwszym pełnym syncu przejść na tryb zmian przyrostowych.

### Etap 5: Dodać odporność na restart i kolejność startu
- Każde urządzenie ma zapamiętać ostatni `bootId` peer'a.
- Stare ramki z poprzedniej sesji mają być ignorowane.
- Po restarcie handshake ma zacząć się od nowa bez ręcznej interwencji.

### Etap 6: Ujednolicić potwierdzenia
- Każda ważna ramka stanu powinna dostać ack.
- Jeśli ack nie przyjdzie, ramka idzie ponownie.
- Krytyczne rzeczy, takie jak ustawienia i pierwsza synchronizacja zegara, nie mogą być „fire-and-forget”.

### Etap 7: Dodać telemetrię debugową
- Logować: bootId, licznik retry, czas do pierwszego syncu, liczbę błędnych CRC, ostatni odebrany typ ramki.
- Dodać jeden wspólny debug format po obu stronach, żeby porównywanie logów było proste.

## 5. Kolejność wdrożenia

1. Najpierw ustalić jeden wspólny kontrakt ramek i stanów.
2. Potem dodać handshake `HELLO/HELLO_ACK`.
3. Następnie przenieść start odbioru komunikacji na możliwie wczesny etap bootu.
4. Później dodać retry i potwierdzenia dla pierwszego syncu.
5. Na końcu dopiero porządkować UI, logi i dokumentację.

## 6. Kryteria zakończenia

- Po każdym restarcie obie strony dochodzą do `FIRST_SYNC_CONFIRMED` bez ręcznych działań.
- Nie ma znaczenia, który projekt wstanie pierwszy.
- Po zaniku łączności połączenie samo wraca.
- Pierwsze dane po starcie są zawsze z tej samej, aktualnej sesji.
- Logi pokazują jednoznacznie, gdzie handshake się zatrzymał, jeśli coś pójdzie źle.

## 7. Dodatkowa uwaga

W [ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main/main/communication/communication.md](ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main/main/communication/communication.md) opis architektury nie odpowiada już temu, co robi aktualny kod. Po naprawie protokołu warto ten plik zaktualizować, żeby dokumentacja nie wprowadzała w błąd.