# Spécification du protocole Goodix 27c6:5117

Document de travail rédigé à partir de l'analyse du driver communautaire
`gulp/libfprint-1`, branche `goodix-5117-support`, le 19/09/2026.

**Ce document remplace le dépôt de référence.** À partir d'ici on code depuis
cette spec uniquement, sans regarder le C de qui que ce soit.

---

## Comment lire ce document

Il est complet volontairement : une fois le dépôt de référence fermé, il n'y a
plus de filet. Mais il n'est **pas fait pour être lu d'un bloc**.

| Section | Quand la lire |
|---|---|
| §0 Matériel | **maintenant** |
| §1 Couche USB | **maintenant** |
| §2 Format des trames | étape 3 |
| §3 Le dialogue (ACK) | étape 3 |
| §4 Les commandes | en référence, au besoin |
| §5 Séquence d'init | étape 3 |
| §6 TLS | étape 4 |
| §7 Capture | étape 5 |
| §8 Format de l'image | étape 5 |
| §9 Constantes binaires | annexe, copier tel quel |
| §10 Pièges | à relire avant chaque étape |

Les sections 6 à 8 vont paraître obscures aujourd'hui. C'est normal et sans
conséquence : on ne s'en sert pas avant des semaines.

---

## §0 — Le matériel

| | |
|---|---|
| Capteur | Goodix, VID `0x27c6`, PID `0x5117` |
| Nom USB | "Goodix Fingerprint Device" |
| Version USB | 2.0 |
| Zone scannée | 88 × 80 pixels |
| Image utile après recadrage | 64 × 80 pixels |
| Profondeur | 12 bits par pixel (valeurs 0–4095) |

L'image est minuscule. C'est une contrainte structurante : elle est trop petite
pour l'algorithme de minuties classique, ce qui imposera une comparaison par
descripteurs visuels (étape 7).

---

## §1 — Couche USB

### Topologie constatée sur la machine cible

```
Device 27c6:5117
├── Interface 0  — classe 2 (Communications)
│   └── EP 0x82  IN   interrupt   8 octets
└── Interface 1  — classe 10 (CDC Data)
    ├── EP 0x01  OUT  bulk       64 octets     <-- on écrit ici
    └── EP 0x81  IN   bulk       64 octets     <-- on lit ici
```

**Tout le protocole passe par les deux endpoints bulk de l'interface 1.**
L'endpoint interrupt `0x82` de l'interface 0 n'est jamais utilisé.

Rappel de convention : IN et OUT sont nommés du point de vue du PC. Pour
envoyer une commande au capteur, on écrit sur l'endpoint **OUT** `0x01`.

### Divergence connue avec le driver de référence

Le driver de référence déclare vouloir travailler sur l'**interface 0**, alors
que les endpoints qu'il utilise (`0x01` / `0x81`) appartiennent à l'**interface
1** d'après les descripteurs lus sur la machine cible.

→ **Tranché à l'étape 2 (19/09/2026).** Les deux interfaces acceptent
`libusb_claim_interface()`. Le driver de référence n'échouait donc pas, mais
verrouillait la mauvaise. **On travaille sur l'interface 1.**

### Environnement de développement

Règle udev en place depuis le 19/09/2026, qui donne l'accès au groupe `plugdev`
sans passer par root :

```
/etc/udev/rules.d/60-goodix-fingerprint.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="27c6", ATTR{idProduct}=="5117", MODE="0660", GROUP="plugdev"
```

Pour l'annuler : supprimer le fichier, puis `udevadm control --reload-rules`
et `udevadm trigger --subsystem-match=usb`.

À l'étape 6, basculer vers `TAG+="uaccess"`, qui est ce qu'utilisent les
drivers intégrés au système.

### Points pratiques

- **Aucun driver noyau n'est attaché** aux deux interfaces. Pas de
  `libusb_detach_kernel_driver()` à prévoir.
- `wMaxPacketSize = 64` sur les deux endpoints bulk. C'est la raison d'être du
  remplissage décrit en §2.
- Une réponse peut dépasser 64 octets : elle arrive alors **fragmentée en
  plusieurs paquets de 64**. Il faut boucler en lecture et accumuler jusqu'à
  disposer d'une trame complète (cf. §2).
- Le driver de référence poste des lectures de 65536 octets maximum.
- Délai d'attente usuel pour une réponse : **1000 ms**.

---

## §2 — Format des trames

Deux couches imbriquées. La couche externe est l'enveloppe de transport, la
couche interne porte la commande.

### Couche externe — « pack »

C'est ce qui est réellement écrit sur l'endpoint USB.

```
 octet :  0      1      2      3      4 ...
         +------+------+------+------+------------------+
         |flags |  longueur (LE16)   |cksum | charge utile |
         +------+------+------+------+------------------+
```

- `flags` : `0xA0` = message de protocole · `0xB0` = données TLS
- `longueur` : taille de la charge utile, little-endian sur 16 bits
- `cksum` : somme 8 bits **des 3 octets d'en-tête uniquement** (flags +
  longueur), avec débordement silencieux. La charge utile n'est pas couverte.
- **Remplissage** : la trame complète est complétée par des zéros jusqu'au
  multiple de 64 supérieur avant d'être envoyée.

Décodage en lecture : une trame est complète dès qu'on a reçu au moins
`4 + longueur` octets. Tant que ce n'est pas le cas, on continue à lire.

### Couche interne — « protocol »

Contenu de la charge utile d'un pack marqué `0xA0`. **Pas de remplissage à ce
niveau.**

```
 octet :  0      1      2      3 ...              n
         +------+------+------+-----------------+------+
         | cmd  |  longueur (LE16)   | charge utile |cksum |
         +------+------+------+-----------------+------+
```

- `cmd` : l'octet de commande (liste en §4)
- `longueur` : **taille de la charge utile + 1**, little-endian. Le `+1` compte
  l'octet de checksum final.
- `cksum` : `0xAA - somme8(cmd + longueur + charge utile)`

Certaines commandes utilisent une valeur fixe `0x88` à la place du checksum
calculé (« checksum nul »). C'est le cas du `NOP`. Les autres commandes
calculent le checksum normalement.

### Exemple complet

Demander la version du firmware : commande `0xA8`, charge utile de 2 octets nuls.

Couche protocol :
```
  a8 03 00 00 00 ff
  ^^ ^^^^^ ^^^^^ ^^
  |    |     |    checksum = 0xAA - (0xA8+0x03+0x00+0x00+0x00) = 0xFF
  |    |     charge utile (2 octets nuls)
  |    longueur = 2 + 1 = 3
  commande
```

Couche pack, avec les 6 octets ci-dessus en charge utile :
```
  a0 06 00 a6 | a8 03 00 00 00 ff | 00 00 ... 00
  ^^ ^^^^^ ^^                       ^^^^^^^^^^^^
  |    |   checksum = 0xA0+0x06+0x00 = 0xA6      remplissage jusqu'à 64
  |    longueur = 6
  flags
```

Total envoyé sur l'endpoint `0x01` : **64 octets**.

---

## §3 — Le dialogue

C'est le point le plus important pour l'étape 3, et le moins intuitif.

**Le capteur répond toujours en deux temps :**

1. Un **accusé de réception** (ACK), commande `0xB0`, qui confirme la bonne
   réception mais ne contient aucune donnée utile.
2. **Puis, seulement pour certaines commandes**, la vraie réponse.

### Format de l'ACK

Charge utile de 2 octets :

```
 octet 0 : l'octet de commande d'origine, renvoyé en écho
 octet 1 : bit 0 — toujours à 1 (sinon : erreur)
           bit 1 — « le MCU n'a pas de configuration »
```

Vérifications à faire : l'écho doit correspondre à la commande envoyée, et le
bit 0 doit être à 1.

### Quelles commandes produisent une vraie réponse

| Réponse après l'ACK | Commandes |
|---|---|
| **Oui** | `0xA8` version firmware · `0xA6` OTP · `0xE4` lecture PSK · `0xA2` reset · `0x90` config MCU · `0x94` fréquence · `0xAE` état MCU · `0x20` image · `0x32`/`0x34`/`0x36` FDT |
| **Non, ACK seul** | `0x96` enable chip · `0x70` mode veille · `0x80` écriture registre |
| **Rien du tout** | `0x00` NOP — n'accuse même pas réception |

Conséquence pratique : après un `NOP`, ne pas attendre de seconde trame — le
programme resterait bloqué jusqu'au délai d'expiration.

### Une commande à la fois

Le protocole ne gère pas plusieurs commandes en parallèle. Il faut avoir reçu
l'ACK — et la réponse quand elle existe — avant d'envoyer la commande suivante.

---

## §4 — Jeu de commandes

| Code | Nom | Charge utile envoyée | Réponse |
|---|---|---|---|
| `0x00` | NOP | 4 octets nuls | non |
| `0x20` | Lire l'image | `01 00` | oui, chiffrée |
| `0x32` | Passer en FDT « down » | tableau de mode | oui |
| `0x34` | Passer en FDT « up » | tableau de mode | oui |
| `0x36` | Passer en mode FDT | tableau de mode | oui |
| `0x50` | NAV 0 | — | oui |
| `0x70` | Passer en veille | `sleep(1) + 00` | non |
| `0x80` | Écrire un registre capteur | multiples(1) + adresse(LE16) + valeur(LE16) | **non** |
| `0x82` | Lire un registre capteur | multiples(1) + adresse(LE16) + longueur(1) + 00 | oui |
| `0x90` | Charger la config MCU | le blob de 256 octets (§9) | oui |
| `0x94` | Fréquence de scan en veille | fréquence (LE16) | oui |
| `0x96` | Activer la puce | `enable(1) + 00` | non |
| `0xA2` | Reset | bit 0 = reset capteur, bit 1 = reset MCU, puis sleep(1) | oui |
| `0xA6` | Lire l'OTP | 2 octets nuls | oui, 64 octets |
| `0xA8` | Version du firmware | 2 octets nuls | oui, chaîne |
| `0xAE` | Interroger l'état du MCU | `0x55` | oui |
| `0xB0` | ACK | *(reçu uniquement)* | — |
| `0xD0` | Demander une connexion TLS | — | oui |
| `0xD4` | TLS établi | — | non |
| `0xE0` | **Écrire la PSK** | flags(LE32) + longueur(LE32) + données | oui |
| `0xE4` | Lire la PSK | flags(LE32) + longueur(LE32) | oui |

**`0xE0` est la seule commande de ce tableau dont l'effet est durable.** Voir
§10. On ne l'implémente pas.

« FDT » signifie *Finger Detection* : le capteur dispose de modes matériels de
détection de présence du doigt.

---

## §5 — Séquence d'initialisation

Dix étapes, dans cet ordre strict.

| # | Commande | Détail |
|---|---|---|
| 1 | `0x00` NOP | remet à zéro l'état de commande du capteur |
| 2 | `0x96` activer la puce | charge utile `01 00` |
| 3 | `0x00` NOP | |
| 4 | `0xA8` version firmware | **`GF_ST411SEC_APP_12117` sur la machine cible** |
| 5 | `0xE4` lire la PSK | flags `0xBB020003`, longueur 0 |
| 6 | `0xA2` reset | reset capteur activé, sleep = 20 |
| 7 | `0x70` mode veille | sleep = 20 |
| 8 | `0xA6` lire l'OTP | puis 4 écritures de registres, voir ci-dessous |
| 9 | `0x90` charger la config MCU | blob de 256 octets (§9) |
| 10 | `0x94` fréquence de scan | valeur 100 |

Avant l'étape 1, il faut démarrer la boucle de lecture sur l'endpoint `0x81`.

**Séquence validée de bout en bout le 19/09/2026.** Relevés sur la machine
cible : OTP de 64 octets, valeurs de calibration `0ba8 00bb 00ba 00ba`. Le
transfert de la config MCU (étape 9) produit une trame de 320 octets, soit cinq
paquets USB, découpés automatiquement par libusb — un seul appel suffit.

### Étape 8 en détail — la calibration d'usine

L'OTP (*One Time Programmable*) est une zone mémoire programmée en usine,
**propre à ta puce**. On lit ses 64 octets, puis on réinjecte 4 valeurs dans
les registres du capteur via la commande `0x80` :

| Adresse du registre | Valeur à écrire |
|---|---|
| `0x0220` | `(otp[46] << 4) \| 8` |
| `0x0236` | `otp[47]` |
| `0x0238` | `otp[48]` |
| `0x023A` | `otp[49]` |

C'est une transcription de paramètres analogiques du capteur. Ces valeurs
seront différentes des miennes, des tiennes, de celles de n'importe qui — c'est
attendu.

### Vérification de la PSK (étape 5)

**Format de la réponse, relevé le 19/09/2026 — 41 octets :**

```
 offset 0    : statut (0 = succès)
 offset 1-4  : flags, LE32 — vaut 0xBB020003
 offset 5-8  : longueur, LE32 — vaut 32
 offset 9-40 : la valeur de 32 octets
```

Le driver de référence exigeait que les flags, la longueur et la valeur
correspondent tous les trois à ses constantes.

**Sur la machine cible, la valeur diffère de celle d'usine** — voir §10,
risque n°1. **Ne rien écrire.**

---

## §6 — La couche TLS *(étape 4 — à ignorer pour l'instant)*

### Ce qui est chiffré et ce qui ne l'est pas

- Toutes les commandes de §4 circulent **en clair** (packs `0xA0`).
- Seules **les données d'image** sont chiffrées (packs `0xB0`).

La commande `0x20` qui demande une image part donc en clair ; c'est la frame
qui revient qui est chiffrée.

### Les paramètres — mesurés le 19/09/2026

Le `ClientHello` réel du capteur donne tout :

| | |
|---|---|
| Version | TLS 1.2 (`0x0303`), record et client |
| Suites proposées | **une seule : `0x00A8` TLS_PSK_WITH_AES_128_GCM_SHA256** |
| Plus `0x00FF` | signalisation de renégociation, pas une suite |
| Extensions | **aucune** |
| Session id | vide |
| Compression | nulle |
| **La clé** | **32 octets à zéro** |

`TLS_PSK_WITH_AES_128_GCM_SHA256` est du PSK **pur** : ni certificat, ni
Diffie-Hellman. Le secret maître dérive directement de la clé pré-partagée.

**Conséquence directe : une clé divergente fait échouer la négociation au
message `Finished`**, et il n'existe aucune autre suite de repli.

Configuration OpenSSL correspondante côté serveur : TLS 1.2 forcé,
`SSL_CTX_set_cipher_list()` sur `PSK-AES128-GCM-SHA256`, un callback PSK
serveur, aucun certificat requis.

`ClientHello` observé, 52 octets :

```
  16 03 03 00 2f 01 00 00 2b 03 03 68 48 89 2b a7
  56 03 6c ff b7 cd 08 3f d4 77 34 2b a5 a3 70 f1
  ba e4 28 fc 41 83 7d d9 6f e1 0a 00 00 04 00 a8
  00 ff 01 00
```

La clé n'est pas scellée dans le matériel, elle n'est pas dérivée, elle n'est
pas secrète. C'est ce qui rend ce projet faisable.

La constante de 32 octets de §9 n'est **pas** la clé : c'est une valeur de
contrôle stockée dans le capteur, qu'on relit avec `0xE4` pour vérifier qu'il
est dans son état d'usine.

### Le sens de la connexion

**Le capteur est le client TLS. Le PC est le serveur.** C'est contre-intuitif
et ça conditionne toute l'implémentation.

Déroulé :

1. Le PC envoie `0xD0`. **Le capteur l'acquitte normalement** (ACK `0xB0`,
   écho `0xD0`, drapeaux `0x01`) — confirmé le 19/09/2026.
2. Le capteur répond avec son *ClientHello*, dans une enveloppe `0xB0`, **pas**
   `0xA0`. Le décodeur doit donc lire l'enveloppe brute avant d'interpréter.
   Puis il se tait et attend le `ServerHello` : une lecture supplémentaire
   expire au bout d'une seconde.
3. Cinq allers-retours de négociation, transportés dans des packs `0xB0`.
4. Le PC envoie `0xD4` (TLS établi).

Les cinq étapes, dans l'ordre : `ServerHello` (PC → capteur), échange de clés
(capteur → PC), changement de chiffrement client (capteur → PC), fin de
négociation client (capteur → PC), changement de chiffrement serveur
(PC → capteur).

### Montage retenu — BIO mémoire, sans thread *(19/09/2026)*

Le driver de référence lance un serveur OpenSSL dans un thread relié par une
`socketpair()`. **On a retenu l'autre approche** : deux BIO mémoire, aucun
thread, aucun descripteur. `SSL_do_handshake()` rend la main avec
`SSL_ERROR_WANT_READ` au lieu de bloquer, et notre boucle fait le transport.

Motif : une négociation abandonnée coûte un démarrage à froid (§10). Pouvoir
inspecter chaque octet et rejouer pas à pas prime sur tout le reste.

Détails d'implémentation qui comptent :

- `SSL_CTX_set_security_level(ctx, 0)` — le PSK pur n'offre pas de
  confidentialité persistante et le niveau par défaut d'Ubuntu le refuse.
- `BIO_set_mem_eof_return(bio, -1)` — sans ça un tampon vide se lit comme une
  fin de fichier et la négociation avorte.
- Callback PSK **ancienne génération** (`SSL_CTX_set_psk_server_callback`),
  celle de TLS 1.2. La nouvelle ne sert qu'à TLS 1.3.
- Toujours vider le tampon de sortie **avant** de tester le résultat : même en
  échec, OpenSSL y laisse une alerte à transmettre.

### Signature d'une clé divergente — mesurée hors capteur *(19/09/2026)*

Négociation jouée entre notre serveur et un client factice, en mémoire.

| | clés identiques | clés différentes |
|---|---|---|
| Échange 1 | 118 puis 71 octets | **identique** |
| Échange 2 | 68 puis 242 octets | 68 puis **7 octets d'alerte** |
| Alerte | — | niveau 2, `bad_record_mac` (20) |
| Erreur OpenSSL | — | `Provider routines::cipher operation failed` |
| Suite négociée | `PSK-AES128-GCM-SHA256` | — |

**Le premier échange est rigoureusement identique dans les deux cas.** La
divergence n'apparaît qu'au second, quand le serveur échoue à déchiffrer le
`Finished` du client.

C'est l'empreinte exacte à guetter en 4b-2.

### Négociation contre le vrai capteur — 4b-2 *(19/09/2026)*

**RÉSOLU. Le canal chiffré s'ouvre avec la clé extraite de Windows.**

```
round 0   sensor ->   52   [handshake]              ClientHello
round 1   host   ->   95   [handshake]              ServerHello + HelloDone
          sensor ->   26   [handshake]              ClientKeyExchange
round 2   sensor ->    6   [change cipher spec]
round 3   sensor ->   45   [handshake]              Finished
round 4   host   ->   51   [change cipher spec]     CCS + Finished

handshake complete
  cipher:       PSK-AES128-GCM-SHA256
  psk identity: Client_identity
```

L'identité PSK annoncée par le capteur est **`Client_identity`**, la valeur par
défaut d'OpenSSL. Le serveur n'a donc pas à en vérifier une en particulier.

#### Récupérer la clé d'un capteur apparié par Windows

Un capteur qu'un pilote Windows a apparié ne détient plus la clé d'usine. Plutôt
que de la réécrire — ce qui exige d'effacer le firmware, voir §10 — on lit
celle que Windows utilise déjà.

Elle est rangée dans `C:\ProgramData\Goodix\Goodix_Cache.bin`, chiffrée par
DPAPI avec une entropie dérivée :

```
graine    = les 8 derniers octets du fichier
racine    = XOR de trois constantes extraites de gfspi.dll
entropie  = SHA256(graine)[16:32] || SHA256( SHA256(graine)[0:16] || racine )
clé       = CryptUnprotectData(fichier, entropie, CurrentUser)
```

Le déchiffrement n'est possible que **sous Windows, dans la session où
l'empreinte a été enrôlée** — DPAPI est lié au compte utilisateur.

Outil de référence : <https://gitlab.com/senyakirilovich/goodix-fingerprint-27c6-5117-psk-extraction>

**Rien n'est écrit, ni dans le capteur ni dans Windows. Windows Hello continue
de fonctionner.**

La clé obtenue se dépose dans `clef.txt` à la racine du projet — ignoré par
git, jamais affiché par les outils, et effacé de la mémoire après usage.
Emplacement modifiable par la variable `GOODIX_PSK_FILE`.

#### Ce que donnait la clé d'usine, pour mémoire

**`bad_record_mac`. Les 32 zéros ne conviennent pas à ce capteur.**

```
round 0   sensor ->   52   [handshake]              ClientHello
round 1   host   ->   95   [handshake]              ServerHello + HelloDone
          sensor ->   26   [handshake]              ClientKeyExchange
round 2   sensor ->    6   [change cipher spec]
round 3   sensor ->   45   [handshake]              Finished, chiffré
round 4   host   ->    7   [alert] level 2, bad_record_mac (20)
```

La négociation se déroule intégralement et n'échoue qu'au déchiffrement du
`Finished` du capteur — exactement la signature reproduite hors capteur.

**Conclusion définitive : Windows Hello a bien reprovisionné le capteur.** La
question ouverte n°4 est tranchée. Les étapes 5 à 7 sont bloquées tant que la
clé n'est pas retrouvée, puisque l'image ne transite que par ce canal.

**Hypothèse réfutée :** l'alerte `bad_record_mac`, pourtant bien transmise, ne
suffit **pas** à libérer le capteur. Il reste figé et exige un démarrage à
froid, au même titre qu'un abandon en plein milieu. Le reset de port et la
vidange restent sans effet.

---

## §7 — Séquence de capture *(étape 5)*

Relevée dans `goodix-fp-dump`, `driver_51x7.py` — **du code spécifique au
5117**, plus détaillé que ce que montrait le fork libfprint.

Tout se déroule **après** l'ouverture du canal chiffré (§6).

### Préambule, en clair

| Commande | Attendu |
|---|---|
| `0xA2` reset (capteur, sleep 20) | succès, et le nombre **2048** |
| `0x82` lire le registre `0x0000`, 4 octets | **`a2 04 25 00`** — identifiant de puce |
| `0xA6` lire l'OTP | au moins 32 octets |
| `0xA2` reset à nouveau | succès, 2048 |
| `0x90` config MCU | succès |
| `0x94` fréquence de veille, 100 | succès |

Puis la négociation TLS, puis `0xD4`.

### Capture proprement dite

```
0x00  nop
0xAE  query mcu state                     charge utile 0x55
0x36  switch to fdt mode                  séquence 1
0x50  nav
0x36  switch to fdt mode                  séquence 2
0x82  lire le registre 0x0082, 2 octets
0x80  écrire 4 registres de calibration   voir ci-dessous
0x20  get image                 ->  IMAGE 1 : « clear-0 », sans doigt
0x36  switch to fdt mode                  séquence 3
0x32  switch to fdt down                  séquence 4
0x00  nop
0xAE  query mcu state
      ---- on attend le doigt ----
0x32  switch to fdt down                  séquence 5
0x20  get image                 ->  IMAGE 2 : l'empreinte
0x34  switch to fdt up                    séquence 6
0x20  get image                 ->  IMAGE 3 : « clear-1 », après retrait
0x50  nav
0x32  switch to fdt down                  séquence 7
```

La dernière ligne n'est pas décorative : le code de référence la commente
« requise après la capture pour que le périphérique continue de répondre ».

### `fdt down` bloque jusqu'à ce qu'un doigt arrive

Mesuré le 20/09/2026. **Les deux appels `0x32`, séquences 4 et 5, ne
répondent pas tant que le capteur ne sent rien** — c'est la détection de doigt
matérielle, et elle n'a pas besoin d'être interrogée en boucle.

Le code de référence affiche son message « Waiting for finger » **après** le
premier `fdt down`, ce qui laisse croire que seule la séquence 5 attend. C'est
faux : la 4 attend déjà. L'invite doit donc être affichée avant elle.

Conséquence : le délai d'attente USB doit être relevé autour de ces deux
commandes — une seconde suffit partout ailleurs, pas pour attendre un humain.

### Les sept séquences FDT

Charges utiles opaques, à rejouer telles quelles.

```
1  0d 01 b2 b2 c2 c2 a7 a7 b6 b6 a6 a6 b6 b6   fdt mode
2  0d 01 80 b0 80 c0 80 a4 80 b4 80 a3 80 b4   fdt mode
3  0d 01 80 b1 80 c1 80 a6 80 b6 80 a5 80 b6   fdt mode
4  0c 01 80 b1 80 c1 80 a6 80 b6 80 a5 80 b6   fdt down
5  0c 01 80 b2 80 c2 80 a7 80 b6 80 a6 80 b6   fdt down, attente du doigt
6  0e 01 80 90 80 9a 80 7b 80 95 80 8c 80 a3   fdt up
7  0c 01 80 a7 80 b9 80 a3 80 b5 80 a4 80 b6   fdt down, remise en état
```

Le premier octet suit le code de commande : `0x0d` pour `0x36`, `0x0c` pour
`0x32`, `0x0e` pour `0x34`.

### Les registres de calibration

Le code de référence écrit des valeurs **en dur**, relevées sur la machine de
son auteur :

```
0x0220 <- 0x0b78     0x0236 <- 0x00b9
0x0238 <- 0x00b8     0x023a <- 0x00b7
```

**Ne pas les recopier.** Ce sont les mêmes registres que ceux de l'init (§5),
et il faut y mettre les valeurs dérivées de **notre** OTP. Sur la machine
cible : `0ba8 00bb 00ba 00ba`. Les valeurs de l'auteur en sont proches, ce qui
confirme qu'elles sont propres à chaque exemplaire.

### Vérifié sur la machine cible *(19/09/2026, étape 5a)*

| | |
|---|---|
| Identifiant de puce, registre `0x0000` | `a2 04 25 00` — conforme |
| Frame déchiffrée | **10 573 octets, exactement l'attendu** |
| En-tête | `20 4a 29 00 00 00 00 00` |
| Pixels décodés | **7040 = 88 × 80** |
| Plage des valeurs | 0 à 3143, moyenne 1991 sur 12 bits |

Les valeurs varient continûment le long d'une ligne, ce qui confirme un vrai
gradient analogique et non du bruit : le décodage 6 octets pour 4 pixels est
donc correct.

### Deux pièges de séquencement relevés à l'étape 5a

**`0xD4` n'est pas acquitté après une négociation réussie.** Le capteur
l'acquitte normalement quand il est en attente (c'est le déverrouillage de
§10), mais reste muet quand le canal vient de s'ouvrir. **Ne pas traiter ce
silence comme une erreur** : le canal est déjà utilisable.

**L'état du capteur survit à la fin du processus.** Corrigé le 21/09/2026 :
`goodix_session_close()` envoie `0xD4` dans tous les cas. Deux sessions
consécutives s'enchaînent sans intervention.

### Le transport de l'image

`0x20` part **en clair** (enveloppe `0xA0`) et reçoit un ACK normal. L'image,
elle, revient dans une enveloppe **`0xB0`** : c'est du chiffré, à passer à
`SSL_read`. Notre `goodix_receive_pack()` gère déjà les deux.

## §8 — Format de l'image *(étape 5)*

### Question tranchée : 88 de large sur 80 de haut

`goodix-fp-dump` déclare `SENSOR_WIDTH = 80` et `SENSOR_HEIGHT = 88`, ce qui
contredit en apparence le fork libfprint. **Les deux noms sont inversés chez
eux** : son `write_pgm` écrit l'en-tête `P2\n{height} {width}` et retourne à la
ligne tous les `height` pixels, donc **88 pixels par ligne, 80 lignes**.

C'est le fork libfprint qui avait raison : **88 × 80**, soit 7040 pixels.

### Taille de la frame brute

```
8 octets d'en-tête + (7040 / 4) × 6 + 5 octets de queue = 10 573 octets
```

Découpage : on jette les 8 premiers et les 5 derniers, il reste 10 560 octets
utiles.

### Décodage des pixels

Pixels sur **12 bits**, empaquetés **6 octets pour 4 pixels**, entrelacés. Pour
chaque groupe `c[0..5]` :

```
 pixel 0 = ((c[0] & 0x0F) << 8) | c[1]
 pixel 1 = (c[3] << 4) | (c[0] >> 4)
 pixel 2 = ((c[5] & 0x0F) << 8) | c[2]
 pixel 3 = (c[4] << 4) | (c[5] >> 4)
```

Identique dans les deux implémentations de référence. Valeurs de 0 à 4095.

### 24 colonnes mortes — mesuré le 19/09/2026

Le capteur renvoie 88 échantillons par ligne, mais **seuls les 64 premiers
portent une donnée**. Les colonnes 64 à 87 valent exactement zéro sur toute la
hauteur, vérifié sur une frame réelle :

```
  colonne 63 : moyenne 2448   min 1903   max 2660
  colonne 64 : moyenne    0   min    0   max    0
  colonne 87 : moyenne    0   min    0   max    0
```

C'est ce qui justifie le recadrage 88 → 64 du fork libfprint. **L'image utile
fait 64 × 80.** On garde les 88 colonnes au décodage, on recadre ensuite.

### Deux façons d'écrire le PGM

| | brut | normalisé |
|---|---|---|
| Format | P5, `maxval 4095` | P5, `maxval 255` |
| Par pixel | 2 octets, **gros-boutiste** | 1 octet |
| Traitement | aucun | soustraction du fond, étalement min/max |
| Intérêt | fidèle au capteur | lisible à l'œil |

Le brut est le bon premier jalon : s'il ressemble à quelque chose, le décodage
est juste. Le fork libfprint fait ensuite : soustraction de l'image de
calibration, puis `(valeur - min) × 255 / (max - min)`, puis recadrage à
64 × 80.

Attention, PGM est **gros-boutiste** pour les valeurs sur 16 bits — l'inverse
du protocole Goodix.

## §9 — Constantes binaires

À recopier telles quelles. Aucune n'est devinable ni calculable.

### PSK attendue (32 octets) — 32 octets

```
  ba 1a 86 03 7c 1d 3c 71 c3 af 34 49
  55 bd 69 a9 a9 86 1d 9e 91 1f a2 49
  85 b6 77 e8 db d7 2d 43
```

### Config MCU — 256 octets

```
  70 11 60 71 2c 9d 2c c9 1c e5 18 fd
  00 fd 00 fd 03 ba 00 01 80 ca 00 04
  00 84 00 15 b3 86 00 00 c4 88 00 00
  ba 8a 00 00 b2 8c 00 00 aa 8e 00 00
  c1 90 00 bb bb 92 00 b1 b1 94 00 00
  a8 96 00 00 b6 98 00 00 00 9a 00 00
  00 d2 00 00 00 d4 00 00 00 d6 00 00
  00 d8 00 00 00 50 00 01 05 d0 00 00
  00 70 00 00 00 72 00 78 56 74 00 34
  12 20 00 10 40 2a 01 02 04 22 00 01
  20 24 00 32 00 80 00 01 00 5c 00 80
  00 56 00 04 20 58 00 03 02 32 00 0c
  02 66 00 03 00 7c 00 00 58 82 00 80
  15 2a 01 82 03 22 00 01 20 24 00 14
  00 80 00 01 00 5c 00 00 01 56 00 04
  20 58 00 03 02 32 00 0c 02 66 00 03
  00 7c 00 00 58 82 00 80 1f 2a 01 08
  00 5c 00 80 00 54 00 10 01 62 00 04
  03 64 00 19 00 66 00 03 00 7c 00 01
  58 2a 01 08 00 5c 00 00 01 52 00 08
  00 54 00 00 01 66 00 03 00 7c 00 01
  58 00 89 2e
```

### FDT switch state mode — 13 octets

```
  01 80 af 80 bf 80 a4 80 b8 80 a8 80
  b7
```

**Rôle de chacune :**

- **PSK attendue** — valeur de contrôle lue avec `0xE4` à l'étape 5 de l'init.
  Ce n'est *pas* la clé TLS (qui vaut 32 zéros).
- **Config MCU** — envoyée telle quelle avec `0x90` à l'étape 9 de l'init.
  Contenu opaque : paramètres internes du microcontrôleur.
- **FDT switch state mode** — charge utile des commandes `0x32`, `0x34` et
  `0x36` pendant la capture.

---

## §10 — Pièges et risques

### Aucun risque matériel

Le jeu de commandes de §4 ne contient **aucune commande d'écriture de
firmware** : pas de flash, pas d'effacement, et l'OTP est accessible en lecture
seule (`0xA6`, sans équivalent en écriture). Le capteur ne peut pas être
détruit par une commande malformée.

Pire cas réaliste : le capteur cesse de répondre jusqu'au redémarrage.

### L'unique exception : `0xE0`

`PRESET_PSK_WRITE` réécrit une valeur persistante dans le capteur. C'est la
seule opération durable du protocole, et elle casserait probablement Windows
Hello sur le dual-boot.

**Décision prise : on n'implémente pas `0xE0`.**

### Risque n°1 — la PSK N'EST PLUS celle d'usine *(confirmé le 19/09/2026)*

Valeur lue sur la machine cible, **masquée volontairement** — c'est une donnée
propre à un exemplaire, et la valeur d'origine n'apporte rien à la spec :

```
  xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx
  xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx
```

Elle ne correspond pas à la constante d'usine de §9. Les flags (`0xBB020003`)
et la longueur (32) sont en revanche conformes.

**Hypothèse principale :** Windows Hello a reprovisionné le capteur lors de
l'enrôlement de l'empreinte.

**Réserves à garder en tête :**

1. Rien ne prouve formellement que c'est Windows. L'assembleur du portable
   pourrait livrer une autre valeur d'usine — peu probable, le driver de
   référence échouerait alors pour tout le monde.
2. Le lien entre cette valeur et la clé TLS (32 zéros) reste inconnu. Ce n'est
   pas un hachage simple des zéros, vérifié. **Rien ne garantit donc que le
   TLS échouera.**

**Conduite retenue : ne rien écrire, poursuivre l'init, et trancher au moment
du TLS (étape 4) sur une mesure plutôt que sur une hypothèse.** La commande
`0xE0` reste le seul recours connu, et reste non implémentée.

### ~~Risque n°2~~ — la version du firmware *(levé le 19/09/2026)*

Le capteur renvoie **`GF_ST411SEC_APP_12117`**, soit la valeur que le driver de
référence attendait pour le 5110 — et non le `GF_ST411SEC_APP_12109` que son
auteur avait substitué en croyant l'adapter au 5117.

Ce fork aurait donc **échoué à l'init sur cette machine**, sa comparaison de
chaîne étant stricte.

Conclusion : cette chaîne identifie une version de build déployée par
l'assembleur du portable, pas un modèle de puce. **Ne jamais en faire une
condition bloquante** — la journaliser, éventuellement avertir si elle diffère,
mais continuer.

La réponse inclut le `\0` terminal : 22 octets pour 21 caractères.

### Risque n°3 — l'interface USB

Voir §1. Le driver de référence et les descripteurs matériels ne sont pas
d'accord. Les descripteurs ont raison par construction.

### Négociation abandonnée — le capteur se verrouille, et `0xD4` le libère

**Révisé le 19/09/2026 après mesure. La version précédente de cette section
était fausse sur presque tout.**

Après un `0xD0` laissé sans suite, le capteur n'accepte plus **qu'une seule
commande**. Mesuré :

| Commande | Réponse |
|---|---|
| `0xA8` version firmware | rien |
| `0x96` enable chip | rien |
| `0x00` NOP | rien |
| `0xE4` lecture PSK | rien |
| `0xF2` lecture firmware | rien |
| **`0xD0` demande de connexion TLS** | **ACK normal, puis le ClientHello** |

**Cet état survit à une extinction complète.** Constaté : arrêt, 30 secondes
hors tension, redémarrage avec réénumération USB confirmée au journal
(`new full-speed USB device number 2`) — le capteur restait verrouillé. Il
est donc conservé en mémoire non volatile.

**Le remède tient en une commande : `0xD4` (TLS_SUCCESSFULLY_ESTABLISHED).**
Elle est acquittée normalement et le capteur redevient utilisable
immédiatement, sans redémarrage. C'est aussi ce qui expliquait qu'un
démarrage sous Windows le débloquait : son pilote possède la clé, mène la
négociation à terme et envoie ce même `0xD4`.

Sans effet, pour mémoire : reset de port USB, `clear_halt`, vidange de
l'endpoint, dé-autorisation sysfs, veille/réveil, extinction complète,
alertes TLS `handshake_failure` et `close_notify`.

**Règle d'implémentation : tout outil qui envoie `0xD0` doit envoyer `0xD4`
en sortie, succès ou échec.**

`goodix_session_close()` l'envoie désormais **systématiquement**, y compris
après une négociation réussie. Sans cela l'état survivait à la fin du
processus et l'ouverture suivante échouait sur `unexpected protocol reply` —
tolérable pour des outils lancés à la main, rédhibitoire pour un démon comme
fprintd qui ouvre et ferme le capteur en boucle.

`tools/unlock.c` ne sert plus que de rattrapage après un plantage.

### Le protocole possède bien des commandes d'écriture de firmware

Correction du 19/09/2026 : contrairement à ce que laissait croire le jeu de
commandes du driver libfprint, il en existe quatre, propres au bootloader.

| Code | Nom |
|---|---|
| `0xA4` | `MCU_ERASE_APP` — efface le firmware applicatif |
| `0xF0` | `WRITE_FIRMWARE` |
| `0xF2` | `READ_FIRMWARE` |
| `0xF4` | `CHECK_FIRMWARE` — CRC32-MPEG, pas de signature |

**`0xF2` n'est PAS implémentée par le firmware applicatif du 5117** : testée
le 19/09/2026, aucune réponse. Aucune sauvegarde du firmware en place n'est
donc possible depuis le mode normal.

### Remettre la clé d'usine — procédure connue, non implémentée

Extraite de `goodix-fp-dump`, `driver_51x7.py`. **Non implémentée dans ce
projet, et déconseillée** : voir la mise en garde plus bas.

La clé ne peut pas être écrite pendant que le firmware applicatif tourne. La
séquence complète est :

```
1. 0xA4  effacer l'application       -> il ne reste que le bootloader IAP
                                        (MILAN_ST411SEC_IAP_12001)
2. 0xE0  écrire PSK_WHITE_BOX        flags 0xBB010003, 96 octets
3. 0xE4  relire et comparer          doit renvoyer le PMK_HASH de §9
4. 0xF0  réécrire un firmware        75 032 octets par tranches de 1008
5. 0xF4  vérifier                    CRC32-MPEG du tout
```

Le blob à écrire à l'étape 2 — ni la clé, ni la valeur relue :

```
  ec35ae3abb45ed3f12c4751f1e5c2cc05b3c5452e9104d9f2a3118644f37a04b
  6fd66b1d97cf80f1345f76c84f03ff30bb51bf308f2a9875c41e6592cd2a2f9e
  60809b17b5316037b69bb2fa5d4c8ac31edb3394046ec06bbdacc57da6a756c5
```

C'est une clé en *boîte blanche* : la clé de 32 zéros y est noyée sous une
forme obscurcie que seul le firmware sait déballer.

**Pourquoi c'est déconseillé sur la machine cible :**

- le seul firmware disponible est `GF_ST411SEC_APP_12109` (75 032 octets,
  sous-module `goodix-firmware`), alors que le capteur tourne en **12117**.
  C'est un downgrade vers une version qu'on espère compatible.
- `0xF2` ne répondant pas, **aucune sauvegarde du 12117 n'est possible**.
- en cas d'échec du flash, le code de référence efface et abandonne. Le
  capteur reste alors en bootloader, inutilisable sous les deux OS jusqu'à un
  flash réussi.
- le CRC de l'étape 5 valide le **transfert**, pas la **compatibilité**.

### Pièges d'implémentation

- **Ne pas attendre de réponse après `0x00`, `0x96`, `0x70`** — ACK seul,
  sinon blocage jusqu'au délai d'expiration.
- **Les deux checksums sont différents.** Couche externe : somme simple des 3
  octets d'en-tête. Couche interne : `0xAA - somme`. Facile à intervertir.
- **Le champ longueur de la couche interne vaut charge utile + 1.** Celui de la
  couche externe vaut exactement la longueur. Piège classique.
- **Tout est en little-endian.**
- **Une réponse arrive fragmentée** en paquets de 64 octets. Boucler et
  accumuler.
- **Seule la couche externe est complétée à 64 octets**, jamais la couche
  interne.
- **L'image de calibration est indispensable.** Sans soustraction, la sortie ne
  ressemble à rien.

---

## §11 — Comparaison de deux empreintes *(étape 6)*

### Méthode retenue : corrélation croisée normalisée

Pour chaque décalage `(dx, dy)` d'une fenêtre de ±12 pixels, on mesure sur la
**zone commune** :

```
            moyenne( (a - ā)(b - b̄) )
  corr  =  ─────────────────────────────
                écart_a × écart_b
```

Le score est le maximum sur tous les décalages. Vaut 1 pour deux images
identiques à la luminosité et au contraste près, 0 pour deux images sans
rapport. Soustraire les moyennes et diviser par les écarts-types rend la
mesure insensible à un doigt plus sec ou plus appuyé.

Prétraitement obligatoire avant comparaison : décodage, recadrage,
soustraction du fond, étalement, puis **normalisation locale** sur une fenêtre
de 15 × 15 (§8). Sans elle, le vignettage des bords domine la corrélation.

### La taille du capteur impose la pose

**Découverte décisive du 20/09/2026.** Un premier jeu de 15 captures prises en
décalant volontairement le doigt de quelques millimètres donnait une séparation
de **+0.012** — inexploitable. Le même doigt, posé **naturellement**, donne
**+0.476**.

| Jeu | même doigt | doigts différents | séparation |
|---|---|---|---|
| Poses volontairement décalées | 0.185 | 0.141 | +0.012 |
| **Poses naturelles** | **0.614** | **0.138** | **+0.476** |

Explication : le capteur ne voit que quelques millimètres carrés de peau. Un
décalage délibéré de 3 mm tombe sur une **zone différente du doigt**, et deux
images de régions disjointes ne peuvent pas se ressembler. Ce n'est pas une
limite de l'algorithme.

**Conséquence pour l'enrôlement : demander une pose naturelle et répétée, pas
une exploration du doigt.** C'est l'inverse de ce qu'on ferait sur un grand
capteur.

### Approches écartées, mesurées

| Approche | Séparation |
|---|---|
| Corrélation + recherche de rotation ±24° | +0.026 |
| Appariement par blocs de 16 × 16 avec vote | −0.015 |

La rotation n'apporte presque rien et double le coût. Les blocs de 16 × 16 ne
sont pas assez distinctifs : chacun s'apparie à n'importe quelle crête voisine.

### Contrôles de validité

```
image contre elle-même              1.000
image décalée de (5,3) pixels       1.000
```

Ces deux contrôles doivent figurer dans tout test de non-régression : ils
distinguent un défaut d'algorithme d'un défaut de données.

### Seuil

Mesuré avec 6 gabarits enrôlés, score = meilleur des 6 :

```
bon doigt, pose naturelle :  0.96  0.95  0.95  0.95  0.92  0.30
mauvais doigt             :  0.28  0.26  0.22  ...  0.10
```

**Seuil retenu : 0.40.** Nettement au-dessus du meilleur imposteur observé
(0.28), et sous les poses naturelles (≥ 0.92 sauf une). Un refus coûte un
nouvel essai, une acceptation à tort coûte tout.

Réserve : 16 comparaisons imposteur ne mesurent pas un taux de faux positifs à
1 sur 100 000. Elles montrent l'allure des distributions, pas une garantie
chiffrée.

### La dispersion des gabarits est un risque de sécurité — mesuré

Vérifier en prenant **le meilleur score parmi N gabarits** multiplie les
occasions de se tromper. Mesuré le 20/09/2026, imposteurs = majeur et pouce :

| N | gabarits cohérents | gabarits dispersés |
|---|---|---|
| 1 | 0.233 | 0.382 |
| 8 | **0.281** | 0.414 |
| 15 | — | **0.414** |

Score **moyen** des imposteurs : 0.147 → 0.178 en cohérent, mais
0.183 → **0.285** en dispersé.

**Avec l'enrôlement dispersé, le meilleur imposteur franchit le seuil de
0.40.** Un autre doigt du même utilisateur aurait été accepté.

Ce n'est donc pas le **nombre** de gabarits qui est dangereux, c'est leur
**dispersion** : une collection éparse finit par décrire « des crêtes en
général » plutôt qu'un doigt précis.

**Conséquence : le contrôle de cohérence à l'enrôlement est une mesure de
sécurité, pas une commodité.** `tools/enroll.c` exige un accord de 0.55 avec
les gabarits déjà retenus, redemande jusqu'à 4 fois, puis **écarte** le
toucher plutôt que de le garder.

### Ce que vaut un imposteur du même utilisateur

Les dessins papillaires de deux doigts d'une même personne sont
**indépendants** — ils se forment par des contraintes mécaniques, pas par la
génétique. En revanche ils partagent des propriétés grossières : largeur de
crêtes, humidité, pression.

Pour un comparateur par corrélation, cette similarité grossière rend le test
**légèrement plus difficile** qu'avec un inconnu. La mesure est donc
conservatrice, ce qui est le bon sens de l'erreur.

## §12 — Environnement libfprint *(étape 7)*

### Construction isolée du libfprint système

Sources : <https://gitlab.freedesktop.org/libfprint/libfprint.git>, clonées
dans `libfprint/` (ignoré par git). Version au 20/09/2026 : `v1.94.100-10`.

```
meson setup build --prefix=$HOME/.local/libfprint-dev \
      -Ddrivers=virtual_image \
      -Dintrospection=false -Ddoc=false -Dgtk-examples=false \
      -Dinstalled-tests=false \
      -Dudev_rules=disabled -Dudev_hwdb=disabled
ninja -C build
ninja -C build install        # sans sudo : ne peut pas écrire dans /usr
```

`-Ddrivers` prend une **liste séparée par des virgules**. En n'en compilant
qu'un, la construction complète prend moins de 4 secondes.

Attention au type des options : `udev_rules` et `udev_hwdb` sont des
*features* (`enabled`/`disabled`/`auto`), pas des booléens.

### Isolation vérifiée

L'empreinte MD5 de `/usr/lib/x86_64-linux-gnu/libfprint-2.so.2.0.0` est
**identique avant et après** l'installation. Le paquet Ubuntu (1.94.3+tod1)
n'installant pas d'en-têtes, `pkg-config` ne trouve rien sans notre script —
aucune confusion possible.

Bascule par `. ./libfprint-env.sh`, qui n'agit que sur le shell courant.

**Pour tout annuler :** `rm -rf ~/.local/libfprint-dev libfprint/`.

### Déclarer un driver — le contrat de nommage

Deux tables à compléter, dans deux fichiers :

```
meson.build            drivers_info  += 'goodix5117': {},
libfprint/meson.build  driver_sources += 'goodix5117' : files('drivers/goodix5117.c'),
```

meson **génère** ensuite du C qui déclare, pour chaque driver retenu :

```c
extern GType (fpi_device_<nom>_get_type) (void);
```

**Le nom dans meson est donc un contrat avec `G_DEFINE_TYPE`.** Pour
`goodix5117`, il faut `G_DEFINE_TYPE (FpiDeviceGoodix5117,
fpi_device_goodix5117, FP_TYPE_IMAGE_DEVICE)`. Toute divergence se solde par
une erreur d'édition de liens.

### Les cinq points d'entrée et leurs réponses

```c
struct _FpImageDeviceClass {
  gint bz3_threshold, img_width, img_height;
  void (*img_open)     (FpImageDevice *dev);
  void (*img_close)    (FpImageDevice *dev);
  void (*activate)     (FpImageDevice *dev);
  void (*change_state) (FpImageDevice *dev, FpiImageDeviceState state);
  void (*deactivate)   (FpImageDevice *dev);
};
```

Chacune doit **rendre la main immédiatement** et rappeler plus tard sa
fonction de complétion : `fpi_image_device_open_complete()`,
`activate_complete()`, `deactivate_complete()`, `close_complete()`. Pendant la
capture : `report_finger_status()`, `image_captured()`, `retry_scan()`,
`session_error()`.

### Correspondance avec notre bibliothèque

| libfprint | notre code |
|---|---|
| `img_open` | `goodix_open` + `goodix_session_open` |
| `activate` | `goodix_capture_prepare` + capture du fond |
| `AWAIT_FINGER_ON` | `goodix_capture_wait_finger` |
| `CAPTURE` | `goodix_capture_frame` |
| `AWAIT_FINGER_OFF` | `goodix_capture_wait_lift` |
| `deactivate` | `goodix_session_close` |

La correspondance est exacte, parce que la machine à états est dictée par le
capteur et non par le framework.

**Seul obstacle : nos fonctions bloquent.** `goodix_capture_wait_finger`
attend jusqu'à 20 s dans `libusb_bulk_transfer`. Inacceptable dans un point
d'entrée asynchrone. Résolution prévue à l'étape 7c, par un thread dédié.

### Détection vérifiée le 20/09/2026

```
device 0
  driver    : goodix5117
  name      : Goodix 5117 Fingerprint Sensor
  scan type : press
```

Outil : `make fplist` après `. ./libfprint-env.sh`.

L'avertissement « Failed to disable USB persist » est sans conséquence :
libfprint essaie d'écrire dans `/sys/.../power/persist`, ce qui demande les
droits root.

### Montage à thread — étape 7c

Nos fonctions bloquent, les points d'entrée de libfprint ne le peuvent pas.
Un thread de travail porte le blocage, un canal de commandes le réveille, et
les résultats reviennent par le contexte principal.

```
   thread principal                      thread de travail
   change_state(CAPTURE)
        │  pose la commande sous mutex
        └── g_cond_signal ────────────►  g_cond_wait se réveille
   (rend la main aussitôt)                goodix_capture_frame()  ← bloque
        ◄── g_main_context_invoke ─────   résultat prêt
   image_captured(img)
```

**Règle unique : `fpi_image_device_*` ne s'appelle que depuis le thread
principal.** Le pont est `g_main_context_invoke()`, jamais `g_idle_add()` —
il faut viser le contexte que libfprint utilise, capturé à l'ouverture par
`g_main_context_ref_thread_default()`.

`g_cond_wait()` relâche le mutex pendant le sommeil et le reprend au réveil.
Sans cette propriété, le travailleur dormirait en tenant le verrou et le
thread principal ne pourrait jamais rien poster.

Chaque résultat en vol tient une référence sur le périphérique
(`g_object_ref`), sinon il pourrait être finalisé avant la livraison.

### Correspondance états / commandes

| État libfprint | Commande | Fonction appelée |
|---|---|---|
| `img_open` | `WORK_OPEN` | `goodix_open` + `session_open` + `capture_prepare` |
| `activate` | `WORK_ACTIVATE` | `goodix_capture_frame` → **fond** |
| `AWAIT_FINGER_ON` | `WORK_AWAIT_FINGER_ON` | `goodix_capture_wait_finger` |
| `CAPTURE` | `WORK_CAPTURE` | `goodix_capture_frame` → **doigt** |
| `AWAIT_FINGER_OFF` | `WORK_AWAIT_FINGER_OFF` | `goodix_capture_wait_lift` |
| `img_close` | `WORK_CLOSE` | `session_close` + `goodix_close` |

`activate` arrive avant toute demande de doigt : c'est le bon moment pour
enregistrer le fond que la soustraction exige.

### Limite assumée : latence de l'annulation

`deactivate` n'est traité qu'une fois l'opération en cours terminée. Si le
travailleur attend un doigt, l'arrêt peut prendre jusqu'à 20 secondes.
Mesuré, correct, mais inélégant.

Les deux autres voies — réarmer `fdt down` en boucle courte, ou passer aux
transferts asynchrones de libusb — n'ont pas été évaluées.

### Intégration des sources

Le driver doit vivre dans l'arbre libfprint, donc les sources y sont
**recopiées** par `make sync-driver`. Ce dépôt reste le seul endroit où on les
édite. Les en-têtes vont dans `drivers/goodix5117/goodix/`, et
`include_directories('drivers/goodix5117')` est ajouté aux dépendances pour
que les `#include "goodix/..."` se résolvent.

Dépendances ajoutées à libfprint : `openssl` et `libusb-1.0`.

### Cycle complet validé le 20/09/2026

`examples/img-capture` a parcouru toute la machine à états à travers le
driver :

```
open completed (error: none)
activation completed
finger status  NONE -> NEEDED
finger status  NEEDED -> NEEDED | PRESENT
Image device captured an image
Minutiae scan completed in 0.002261 secs
finger status  PRESENT -> NONE
deactivation completed
written to 'empreinte.pgm'
```

Image obtenue : PGM 64 × 80, crêtes nettes. Le chemin complet — USB, TLS,
capture, soustraction du fond, rehaussement — fonctionne sous le contrôle de
libfprint.

### La comparaison par défaut de libfprint est inutilisable — mesuré

Extraction NBIS lancée sur les images réelles le 20/09/2026 :

| Image | Minuties |
|---|---|
| `empreinte.pgm` (capture libfprint) | **1** |
| `fingerprint.pgm` | 0 — « No minutiae found » |
| `fingerprint-enhanced.pgm` | 0 |

`bozorth3` en réclame une vingtaine. **La chaîne NBIS ne fonctionnera jamais
sur ce capteur**, quel que soit le seuil : avec une minutie, abaisser le seuil
reviendrait à accepter n'importe quel doigt.

### Et libfprint amont n'offre aucun point d'accroche

- `fpi-image-device.c` appelle `fpi_print_bz3_match()` **en dur**, sans
  sélecteur d'algorithme.
- `FPI_PRINT_RAW` existe, mais sa comparaison est une **égalité binaire**
  (`g_variant_equal`) : elle sert aux capteurs qui comparent eux-mêmes, pas à
  brancher un comparateur externe.

C'est précisément pourquoi le projet de référence est un **fork** de libfprint
et non un driver ajouté à l'amont : il a dû introduire un champ `algorithm`
dans `FpImageDeviceClass`.

**Conséquence : l'étape 8 exige de modifier le cœur de libfprint.** Notre
driver seul ne suffit pas.

### Le correctif retenu — deux points d'extension génériques

Plutôt que d'écrire « corrélation » dans libfprint, on lui ajoute deux
fonctions optionnelles dans `FpImageDeviceClass` :

```c
GVariant * (*serialize_image) (FpImageDevice *dev, FpImage *image);
gboolean   (*compare_data)    (FpImageDevice *dev, GVariant *probe,
                               GVariant *enrolled);
```

Un driver qui fournit les deux prend la main : la détection de minuties est
court-circuitée, l'empreinte devient un `FPI_PRINT_RAW` portant ce que le
driver sérialise, et c'est lui qui tranche.

**libfprint ignore tout de notre algorithme**, et aucun autre driver n'est
affecté. Modifications : `fpi-image-device.h` (déclarations),
`fpi-image-device.c` (court-circuit, accumulation à l'enrôlement, branche
identify). Environ 150 lignes, conservées dans
`driver/libfprint-custom-matching.patch`.

Côté driver, `serialize_image` renvoie simplement `image->data` — le gabarit
y est déjà, `build_image()` ayant appliqué toute la chaîne de traitement. Et
`compare_data` parcourt les gabarits enrôlés en s'arrêtant au premier qui
dépasse le seuil.

**Contrôle de cohérence à l'enrôlement.** À partir du deuxième toucher,
libfprint compare le nouveau gabarit aux précédents **via `compare_data`**.
S'il ne correspond à aucun, il renvoie `FP_DEVICE_RETRY_CENTER_FINGER` au lieu
de le garder. Sans ce garde-fou, l'enrôlement de fprintd acceptait tout et les
scores de vérification tombaient à 0.31–0.40 au lieu de 0.85+ — c'est la même
mesure qu'au §11, transposée dans le démon.

Le dépôt reste la source unique : `driver/goodix5117.c` et `src/` sont
recopiés dans l'arbre libfprint par `make sync-driver`.

### Emplacement de la clé

Un driver chargé par fprintd démarre dans un répertoire quelconque. La clé est
donc cherchée dans l'ordre : `$GOODIX_PSK_FILE`, puis `/etc/goodix5117/psk`,
puis `clef.txt` — ce dernier n'étant qu'une commodité de développement.

### Deux pièges du greffon, trouvés à l'usage

**Le type d'empreinte est fixé avant la première capture.**
`fp-image-device.c` type l'empreinte d'enrôlement en `FPI_PRINT_NBIS` dès le
démarrage de l'action. La sérialisation partait donc dans la branche minuties
et écrivait un tableau vide : **73 octets au lieu de 25 Ko**. Corrigé en
choisissant le type selon `serialize_image`. L'hypothèse « tout driver image
passe par NBIS » est câblée à **deux endroits**, pas un.

**`activate` n'est appelé qu'une fois pour cinq touchers.** Le fond capturé à
l'activation était périmé dès le deuxième gabarit, et le capteur dérive en
chauffant (libfprint journalise lui-même `COLD -> WARM`). Scores obtenus :
0.197, 0.136, 0.260, 0.252, 0.252. Le fond est désormais repris **au début de
chaque attente de doigt**, ce qui reproduit la séquence de `sample.c`.

### Résultat

```
correlation 0.846 (threshold 0.40)
Match report: matched finger right index successfully
```

Enrôlement et vérification fonctionnent de bout en bout à travers libfprint,
avec stockage sur disque (25 684 octets pour 5 gabarits).

## §13 — fprintd et PAM *(étape 8)*

### Faire charger notre libfprint à fprintd

Le service applique `ProtectHome=true` : il **ne peut pas lire `/home`**. La
bibliothèque doit donc vivre ailleurs. `/usr/local` convient — `apt` n'y
touche jamais — et un **sous-dossier** évite qu'elle soit trouvée par d'autres
programmes, puisque seul fprintd reçoit le chemin.

```
/usr/local/lib/goodix-libfprint/libfprint-2.so.2     la nôtre
/usr/lib/x86_64-linux-gnu/libfprint-2.so.2           celle d'Ubuntu, intacte
/etc/goodix5117/psk                                  la clé, 0600 root:root
/etc/systemd/system/fprintd.service.d/goodix.conf    Environment=LD_LIBRARY_PATH=...
```

La clé est **plus protégée** là qu'elle ne l'était en `0664` dans le dossier
personnel.

### PAM

`sudo pam-auth-update`, puis cocher *Fingerprint authentication*.

Ubuntu livre `max-tries=1 timeout=10`, alors que le module accepte 3 essais
par défaut : un seul toucher raté retombait aussitôt sur le mot de passe.
Corrigé dans **`/usr/share/pam-configs/fprintd`** — éditer `common-auth`
directement serait écrasé — puis `pam-auth-update --package` :

```
pam_fprintd.so max-tries=3 timeout=20
```

**Ce fichier appartient au paquet `libpam-fprintd`** : une mise à jour le
remettra à `max-tries=1`. Il suffira de rejouer la même commande.

### Seuil retenu en usage réel : 0.50

Mesuré à travers fprintd, avec l'enrôlement final :

| Doigt | Meilleur score | Verdict |
|---|---|---|
| Majeur | **0.394** | refusé |
| Index mal posé | 0.337 | refusé |
| Index bien posé | 0.509, 0.581, 0.738, 0.890 | accepté |

À 0.40, le majeur passait à **0.006 près**. D'où 0.50.

**Fait marquant : le majeur a mieux scoré que l'index mal posé.** Sur une
image de 64 × 80, la qualité de la pose pèse plus que l'identité du doigt. Les
deux distributions se recouvrent davantage que sur un grand capteur.

**Conséquence à assumer : cette authentification est un confort, pas un
renforcement de sécurité.** Même constat pour l'implémentation Windows du même
capteur. Un refus retombe sur le mot de passe, donc ne bloque rien.

### Validé de bout en bout le 21/09/2026

| Point d'entrée | Index bien posé | Majeur |
|---|---|---|
| `sudo` | accepté | refusé |
| Écran de verrouillage | déverrouille | refusé |

Trois essais par invite (`max-tries=3`), repli sur le mot de passe ensuite.

**Le projet atteint son objectif : déverrouillage de session à l'empreinte sur
un capteur sans aucun support Linux amont.**

## §14 — Tests

`make test` — 151 vérifications, aucun matériel requis, moins d'une seconde.

| Fichier | Couvre |
|---|---|
| `tests/test_protocol.c` | checksums, encodage et décodage des deux couches, vecteurs connus, tampons trop petits |
| `tests/test_image.c` | dépaquetage 12 bits, recadrage, soustraction signée, rehaussement, en-têtes PGM et gros-boutisme |
| `tests/test_match.c` | corrélation, invariance au contraste, fenêtre de recherche, fichier d'enrôlement et ses droits |
| `tests/test_session.c` | lecture de la clé : casse, espaces, longueurs invalides, fichier absent |

**Deux contrôles à ne jamais retirer** : une image corrélée avec elle-même vaut
exactement 1.0, et une copie décalée dans la fenêtre aussi. Ce sont eux qui ont
distingué un défaut d'algorithme d'un défaut de données quand les scores réels
étaient mauvais (§11).

### La batterie a été vérifiée par mutation

Une suite qui passe ne prouve rien tant qu'on n'a pas vérifié qu'elle sait
échouer. Trois défauts introduits volontairement, trois détections :

| Mutation | Test qui tombe |
|---|---|
| Checksum `0xAA` → `0xAB` | `encodes_the_message_layer` |
| Recadrage sur 63 colonnes | `crop_keeps_the_left_columns` |
| Fenêtre de recherche 12 → 2 | `a_shift_beyond_the_window_is_not` |

### Deux défauts du Makefile révélés au passage

**Aucun suivi des en-têtes.** Modifier un `.h` ne déclenchait rien : c'est un
binaire périmé qui a fait échouer la première mutation. Corrigé par
`-MMD -MP`.

**Collision de noms de fichiers de dépendances.** `src/match.c` produit
`build/match.o` et `tools/match.c` produit `build/match` — les deux écrivaient
`build/match.d`, le second écrasant le premier. Même chose pour `capture`,
`init` et `image`. Corrigé par `-MF $@.d`, qui ancre le nom sur la cible.

## Statut

| Étape | État |
|---|---|
| 1. Extraction de la spec | **terminée** |
| 2. Énumération USB en C | **terminée** |
| 3a. Une commande, une réponse | **terminée** |
| 3b. Séquence d'init complète | **terminée** |
| 4a. Sonde TLS | **terminée** |
| 4b-1. Négociation validée hors capteur | **terminée** |
| 4b-2. Négociation contre le capteur | **terminée — canal ouvert** |
| 5a. Récupération d'une frame brute | **terminée** |
| 5b. Décodage et écriture PGM | **terminée** |
| 5c. Soustraction du fond, empreinte lisible | **terminée** |
| 6. Comparaison de deux empreintes | **terminée** — enrôlement cohérent 0.95, vérification 0.907 |
| 7a. Build isolé de libfprint | **terminée** |
| 7b. Driver squelette détecté | **terminée** |
| 7c. Thread, init et TLS dans le driver | **terminée** |
| 7d. Capture et image | **terminée** |
| 8a. Comparaison dans libfprint | **terminée** |
| 8b. fprintd | **terminée** |
| 8c. PAM | **terminée** |
| Tests unitaires | **151 vérifications, validées par mutation** |

**Questions encore ouvertes :**

1. ~~Sur quelle interface USB faut-il faire le claim ?~~ **Résolue le 19/09/2026 :
   les deux interfaces acceptent le verrou, aucun conflit. On utilise
   l'interface 1, qui porte les endpoints bulk `0x01` et `0x81`.**
2. ~~Quelle est la version réelle du firmware ?~~ **Résolue le 19/09/2026 :
   `GF_ST411SEC_APP_12117`.**
3. ~~La PSK du capteur est-elle toujours celle d'usine ?~~ **Résolue le
   19/09/2026 : NON.** Voir §10, risque n°1.
4. ~~Le TLS fonctionne-t-il malgré une PSK reprovisionnée ?~~ **Résolue le
   19/09/2026 : OUI, avec la clé extraite de Windows.** Voir §6.
5. ~~Que faut-il écrire avec `0xE0` ?~~ **Résolue : un blob de 96 octets en
   boîte blanche, flags `0xBB010003`, et uniquement depuis le bootloader.**
   Voir §10.
6. ~~Le firmware `12109` est-il compatible avec un `12117` ?~~ **Sans objet :
   la voie du reflashage est abandonnée, la clé se récupère depuis Windows.**
7. ~~80 × 88 ou 88 × 80 ?~~ **Résolue : 88 de large sur 80 de haut.** Les noms
   sont inversés dans `goodix-fp-dump` ; son propre `write_pgm` le démontre.
