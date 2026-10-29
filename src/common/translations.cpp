// Text the recomp adds to the menus in the game's other languages. The game's own menus say "du" in German and use
// its names for gadgets and controls where it has them.
#include "common/translations.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include "common/options.h"

namespace {

// French, German, Italian, Spanish, Dutch, Swedish
constexpr int kLanguageCount = 6;
struct Entry {
    std::string_view english;
    std::u16string_view text[kLanguageCount];
};
constexpr Entry kEntries[] = {
    // pages, sections and the panel's own
    {"RECOMP SETTINGS", {u"Paramètres Recomp", u"Recomp-Einstellungen", u"Impostazioni Recomp", u"Configuración de Recomp", u"Recomp-instellingen", u"Recomp-inställningar"}},
    {"MOUSE AND KEYBOARD", {u"Souris et clavier", u"Maus und Tastatur", u"Mouse e tastiera", u"Ratón y teclado", u"Muis en toetsenbord", u"Mus och tangentbord"}},
    {"CONTROLLER", {u"Manette", u"Controller", u"Controller", u"Controlador", u"Controller", u"Handkontroll"}},
    {"DISPLAY", {u"Affichage", u"Anzeige", u"Video", u"Pantalla", u"Weergave", u"Bild"}},
    {"GRAPHICS", {u"Graphismes", u"Grafik", u"Grafica", u"Gráficos", u"Grafisch", u"Grafik"}},
    {"GAMEPLAY", {u"Jouabilité", u"Gameplay", u"Gameplay", u"Jugabilidad", u"Gameplay", u"Spel"}},
    {"GENERAL", {u"Général", u"Allgemein", u"Generale", u"General", u"Algemeen", u"Allmänt"}},
    {"INPUT DEVICES", {u"Périphériques", u"Eingabegeräte", u"Dispositivi di input", u"Dispositivos de entrada", u"Invoerapparaten", u"Inmatningsenheter"}},
    {"TEXTURES", {u"Textures", u"Texturen", u"Texture", u"Texturas", u"Textures", u"Texturer"}},
    {"MOUSE", {u"Souris", u"Maus", u"Mouse", u"Ratón", u"Muis", u"Mus"}},
    {"KEYBOARD", {u"Clavier", u"Tastatur", u"Tastiera", u"Teclado", u"Toetsenbord", u"Tangentbord"}},
    {"GYRO", {u"Gyroscope", u"Gyroskop", u"Giroscopio", u"Giroscopio", u"Gyroscoop", u"Gyroskop"}},
    {"DRIVING", {u"Conduite", u"Fahren", u"Guida", u"Conducción", u"Rijden", u"Körning"}},
    {"RESTORE DEFAULTS", {u"Rétablir les valeurs par défaut", u"Standard wiederherstellen", u"Ripristina predefiniti", u"Restaurar predeterminados", u"Standaardwaarden herstellen", u"Återställ standardvärden"}},
    {"SETTINGS", {u"Paramètres", u"Einstellungen", u"Impostazioni", u"Configuración", u"Instellingen", u"Inställningar"}},
    {"Back", {u"Retour", u"Zurück", u"Indietro", u"Atrás", u"Terug", u"Tillbaka"}},
    {"Defaults", {u"Par défaut", u"Standard", u"Predefiniti", u"Por defecto", u"Standaard", u"Standard"}},
    {"Apply", {u"Appliquer", u"Übernehmen", u"Applica", u"Aplicar", u"Toepassen", u"Verkställ"}},
    {"PRESS A KEY", {u"Appuyez sur une touche", u"Taste drücken", u"Premi un tasto", u"Pulsa una tecla", u"Druk op een toets", u"Tryck på en tangent"}},
    {"UNBOUND", {u"Non assigné", u"Nicht belegt", u"Non assegnato", u"Sin asignar", u"Niet toegewezen", u"Ej tilldelad"}},
    {"Press a key or mouse button to bind, Escape cancels", {u"Appuyez sur une touche ou un bouton de souris, Échap pour annuler", u"Taste oder Maustaste drücken, Escape bricht ab", u"Premi un tasto o un pulsante del mouse, Esc per annullare", u"Pulsa una tecla o un botón del ratón, Escape cancela", u"Druk op een toets of muisknop, Escape annuleert", u"Tryck på en tangent eller musknapp, Escape avbryter"}},
    {"Right click or Delete clears the key", {u"Clic droit ou Suppr pour effacer la touche", u"Rechtsklick oder Entf löscht die Taste", u"Clic destro o Canc per cancellare il tasto", u"Clic derecho o Supr borra la tecla", u"Rechtsklik of Delete wist de toets", u"Högerklicka eller Delete rensar tangenten"}},

    // RECOMP SETTINGS rows
    {"MONITOR", {u"Écran", u"Monitor", u"Monitor", u"Monitor", u"Monitor", u"Bildskärm"}},
    {"DISPLAY MODE", {u"Mode d'affichage", u"Anzeigemodus", u"Modalità schermo", u"Modo de pantalla", u"Weergavemodus", u"Visningsläge"}},
    {"RESOLUTION", {u"Résolution", u"Auflösung", u"Risoluzione", u"Resolución", u"Resolutie", u"Upplösning"}},
    {"RENDER RESOLUTION", {u"Résolution de rendu", u"Render-Auflösung", u"Risoluzione di rendering", u"Resolución de renderizado", u"Renderresolutie", u"Renderingsupplösning"}},
    {"FIELD OF VIEW", {u"Champ de vision", u"Sichtfeld", u"Campo visivo", u"Campo de visión", u"Gezichtsveld", u"Synfält"}},
    {"VIEWMODEL FIELD OF VIEW", {u"Champ de vision de l'arme", u"Waffen-Sichtfeld", u"Campo visivo dell'arma", u"Campo de visión del arma", u"Gezichtsveld van wapen", u"Vapnets synfält"}},
    {"VERTICAL SYNC", {u"Synchro verticale", u"V-Sync", u"Sincronia verticale", u"Sincronismo vertical", u"Verticale synchronisatie", u"Vertikal synk"}},
    {"HDR OUTPUT", {u"Sortie HDR", u"HDR-Ausgabe", u"Uscita HDR", u"Salida HDR", u"HDR-uitvoer", u"HDR-utgång"}},
    {"MAX FRAME RATE", {u"Images par seconde max.", u"Max. Bildrate", u"Frame rate massimo", u"Fotogramas por segundo máx.", u"Max. framerate", u"Max bildfrekvens"}},
    {"FPS COUNTER", {u"Compteur d'images par seconde", u"FPS-Anzeige", u"Contatore FPS", u"Contador de FPS", u"FPS-teller", u"FPS-räknare"}},
    {"FMV SCALE", {u"Échelle des cinématiques", u"Videoskalierung", u"Scala dei filmati", u"Escala de vídeos", u"Videoschaal", u"Videoskala"}},
    {"COLOUR BLIND MODE", {u"Mode daltonien", u"Farbenblindmodus", u"Modalità daltonici", u"Modo daltónico", u"Kleurenblindmodus", u"Färgblindhetsläge"}},
    {"AMBIENT OCCLUSION", {u"Occlusion ambiante", u"Ambient Occlusion", u"Occlusione ambientale", u"Oclusión ambiental", u"Ambient occlusion", u"Ambient occlusion"}},
    {"ANISOTROPIC FILTERING", {u"Filtrage anisotropique", u"Anisotropische Filterung", u"Filtro anisotropico", u"Filtro anisotrópico", u"Anisotrope filtering", u"Anisotropisk filtrering"}},
    {"SKIP INTROS", {u"Passer les intros", u"Intros überspringen", u"Salta introduzione", u"Saltar introducción", u"Intro's overslaan", u"Hoppa över intron"}},
    {"PAUSE ON FOCUS LOSS", {u"Pause en arrière-plan", u"Pause im Hintergrund", u"Pausa in background", u"Pausa en segundo plano", u"Pauzeren op achtergrond", u"Pausa i bakgrunden"}},
    {"LANGUAGE", {u"Langue", u"Sprache", u"Lingua", u"Idioma", u"Taal", u"Språk"}},
    {"AUDIO OUTPUT", {u"Sortie audio", u"Audioausgabe", u"Uscita audio", u"Salida de audio", u"Audio-uitvoer", u"Ljudutgång"}},
    {"PLAYER", {u"Joueur", u"Spieler", u"Giocatore", u"Jugador", u"Speler", u"Spelare"}},
    {"TEXTURE REPLACEMENT", {u"Remplacement des textures", u"Texturersetzung", u"Sostituzione delle texture", u"Reemplazo de texturas", u"Texturen vervangen", u"Texturersättning"}},
    {"TEXTURE DUMPING", {u"Extraction des textures", u"Texturextraktion", u"Estrazione delle texture", u"Extracción de texturas", u"Texturen exporteren", u"Texturexport"}},
    {"DUMP FORMAT", {u"Format d'extraction", u"Extraktionsformat", u"Formato di estrazione", u"Formato de extracción", u"Exportformaat", u"Exportformat"}},

    // the game's own settings
    {"AUTO AIM", {u"Visée auto", u"Zielhilfe", u"Mira automatica", u"Apuntado automático", u"Automatisch richten", u"Autosikte"}},
    {"CROSSHAIRS", {u"Réticule", u"Fadenkreuz", u"Mirino", u"Punto de mira", u"Vizier", u"Sikte"}},
    {"EFFECTS VOLUME", {u"Volume des effets", u"Effektlautstärke", u"Volume effetti", u"Volumen de efectos", u"Effectvolume", u"Effektvolym"}},
    {"FLASHING OBJECTS", {u"Objets clignotants", u"Blinkende Objekte", u"Oggetti lampeggianti", u"Objetos parpadeantes", u"Knipperende objecten", u"Blinkande föremål"}},
    {"HUD ALWAYS ON", {u"Interface toujours visible", u"HUD immer an", u"HUD sempre attivo", u"HUD siempre visible", u"HUD altijd aan", u"HUD alltid på"}},
    {"MANUAL AIM", {u"Visée manuelle", u"Manuelles Zielen", u"Mira manuale", u"Apuntado manual", u"Handmatig richten", u"Manuellt sikte"}},
    {"MUSIC VOLUME", {u"Volume de la musique", u"Musiklautstärke", u"Volume musica", u"Volumen de la música", u"Muziekvolume", u"Musikvolym"}},
    {"SUBTITLES", {u"Sous-titres", u"Untertitel", u"Sottotitoli", u"Subtítulos", u"Ondertiteling", u"Undertexter"}},
    {"VIBRATION", {u"Vibrations", u"Vibration", u"Vibrazione", u"Vibración", u"Trilling", u"Vibration"}},
    {"WEAPON AUTO SWITCH", {u"Changement d'arme auto", u"Autom. Waffenwechsel", u"Cambio arma automatico", u"Cambio de arma automático", u"Automatisch wapen wisselen", u"Automatiskt vapenbyte"}},

    // values
    {"FULLSCREEN", {u"Plein écran", u"Vollbild", u"Schermo intero", u"Pantalla completa", u"Volledig scherm", u"Helskärm"}},
    {"WINDOWED", {u"Fenêtré", u"Fenster", u"Finestra", u"Ventana", u"Venster", u"Fönster"}},
    {"BORDERLESS", {u"Fenêtré sans bordure", u"Randloses Fenster", u"Finestra senza bordi", u"Ventana sin bordes", u"Randloos venster", u"Kantlöst fönster"}},
    {"ON", {u"Oui", u"Ein", u"Sì", u"Sí", u"Aan", u"På"}},
    {"OFF", {u"Non", u"Aus", u"No", u"No", u"Uit", u"Av"}},
    {"UNLIMITED", {u"Illimité", u"Unbegrenzt", u"Illimitato", u"Ilimitado", u"Onbeperkt", u"Obegränsad"}},
    {"REFRESH RATE", {u"Fréquence de rafraîchissement", u"Bildwiederholfrequenz", u"Frequenza di aggiornamento", u"Frecuencia de actualización", u"Verversingssnelheid", u"Uppdateringsfrekvens"}},
    {"AUTOMATIC", {u"Automatique", u"Automatisch", u"Automatico", u"Automático", u"Automatisch", u"Automatisk"}},
    {"NOT SUPPORTED", {u"Non pris en charge", u"Nicht unterstützt", u"Non supportato", u"No compatible", u"Niet ondersteund", u"Stöds inte"}},
    {"FILL", {u"Remplir", u"Füllen", u"Riempi", u"Rellenar", u"Vullen", u"Fyll"}},
    {"PROTANOPIA", {u"Protanopie", u"Protanopie", u"Protanopia", u"Protanopía", u"Protanopie", u"Protanopi"}},
    {"DEUTERANOPIA", {u"Deutéranopie", u"Deuteranopie", u"Deuteranopia", u"Deuteranopía", u"Deuteranopie", u"Deuteranopi"}},
    {"TRITANOPIA", {u"Tritanopie", u"Tritanopie", u"Tritanopia", u"Tritanopía", u"Tritanopie", u"Tritanopi"}},
    {"GAME DEFAULT", {u"Par défaut du jeu", u"Spielstandard", u"Predefinito del gioco", u"Predeterminado del juego", u"Standaard van spel", u"Spelets standard"}},
    {"2.0 STEREO", {u"2.0 Stéréo", u"2.0 Stereo", u"2.0 Stereo", u"2.0 Estéreo", u"2.0 Stereo", u"2.0 Stereo"}},
    {"5.1 SURROUND", {u"5.1 Surround", u"5.1 Surround", u"5.1 Surround", u"5.1 Envolvente", u"5.1 Surround", u"5.1 Surround"}},
    {"(DEFAULT)", {u"(par défaut)", u"(Standard)", u"(predefinito)", u"(por defecto)", u"(standaard)", u"(standard)"}},
    {"(RESTART)", {u"(redémarrage)", u"(Neustart)", u"(riavvio)", u"(reinicio)", u"(herstart)", u"(omstart)"}},
    {"(PRIMARY)", {u"(principal)", u"(primär)", u"(principale)", u"(principal)", u"(primair)", u"(primär)"}},
    {"PRIMARY", {u"Principal", u"Primär", u"Principale", u"Principal", u"Primair", u"Primär"}},
    {"(UNPLUGGED)", {u"(débranché)", u"(entfernt)", u"(scollegato)", u"(desconectado)", u"(losgekoppeld)", u"(frånkopplad)"}},
    {"NOT CONNECTED", {u"Non connecté", u"Nicht verbunden", u"Non collegato", u"No conectado", u"Niet verbonden", u"Inte ansluten"}},
    {"ENGLISH", {u"Anglais", u"Englisch", u"Inglese", u"Inglés", u"Engels", u"Engelska"}},
    {"FRENCH", {u"Français", u"Französisch", u"Francese", u"Francés", u"Frans", u"Franska"}},
    {"GERMAN", {u"Allemand", u"Deutsch", u"Tedesco", u"Alemán", u"Duits", u"Tyska"}},
    {"ITALIAN", {u"Italien", u"Italienisch", u"Italiano", u"Italiano", u"Italiaans", u"Italienska"}},
    {"SPANISH", {u"Espagnol", u"Spanisch", u"Spagnolo", u"Español", u"Spaans", u"Spanska"}},
    {"DUTCH", {u"Néerlandais", u"Niederländisch", u"Olandese", u"Neerlandés", u"Nederlands", u"Nederländska"}},
    {"SWEDISH", {u"Suédois", u"Schwedisch", u"Svedese", u"Sueco", u"Zweeds", u"Svenska"}},
    {"HOLD", {u"Maintenir", u"Halten", u"Tieni premuto", u"Mantener", u"Vasthouden", u"Håll"}},
    {"TOGGLE", {u"Basculer", u"Umschalten", u"Alterna", u"Alternar", u"Wisselen", u"Växla"}},

    // MOUSE AND KEYBOARD and CONTROLLER rows
    {"HORIZONTAL SENSITIVITY", {u"Sensibilité horizontale", u"Horizontale Empfindlichkeit", u"Sensibilità orizzontale", u"Sensibilidad horizontal", u"Horizontale gevoeligheid", u"Horisontell känslighet"}},
    {"VERTICAL SENSITIVITY", {u"Sensibilité verticale", u"Vertikale Empfindlichkeit", u"Sensibilità verticale", u"Sensibilidad vertical", u"Verticale gevoeligheid", u"Vertikal känslighet"}},
    {"AIM HORIZONTAL SENSITIVITY", {u"Sensibilité horizontale en visée", u"Horizontale Zielempfindlichkeit", u"Sensibilità orizzontale in mira", u"Sensibilidad horizontal al apuntar", u"Horizontale richtgevoeligheid", u"Horisontell siktkänslighet"}},
    {"AIM VERTICAL SENSITIVITY", {u"Sensibilité verticale en visée", u"Vertikale Zielempfindlichkeit", u"Sensibilità verticale in mira", u"Sensibilidad vertical al apuntar", u"Verticale richtgevoeligheid", u"Vertikal siktkänslighet"}},
    {"INVERT X AXIS", {u"Inverser l'axe X", u"X-Achse umkehren", u"Inverti asse X", u"Invertir eje X", u"X-as omkeren", u"Invertera X-axel"}},
    {"INVERT Y AXIS", {u"Inverser l'axe Y", u"Y-Achse umkehren", u"Inverti asse Y", u"Invertir eje Y", u"Y-as omkeren", u"Invertera Y-axel"}},
    {"DEADZONE", {u"Zone morte", u"Totzone", u"Zona morta", u"Zona muerta", u"Dode zone", u"Dödzon"}},
    {"VIBRATION STRENGTH", {u"Intensité des vibrations", u"Vibrationsstärke", u"Intensità vibrazione", u"Intensidad de la vibración", u"Trillingssterkte", u"Vibrationsstyrka"}},
    {"GYRO AIMING", {u"Visée gyroscopique", u"Gyro-Zielen", u"Mira giroscopica", u"Apuntado con giroscopio", u"Richten met gyroscoop", u"Gyrosikte"}},
    {"GYRO SENSITIVITY", {u"Sensibilité du gyroscope", u"Gyro-Empfindlichkeit", u"Sensibilità del giroscopio", u"Sensibilidad del giroscopio", u"Gyroscoopgevoeligheid", u"Gyrokänslighet"}},
    {"GYRO AXIS", {u"Axe du gyroscope", u"Gyro-Achse", u"Asse del giroscopio", u"Eje del giroscopio", u"Gyroscoopas", u"Gyroaxel"}},
    {"GYRO BUTTON", {u"Bouton du gyroscope", u"Gyro-Taste", u"Pulsante del giroscopio", u"Botón del giroscopio", u"Gyroscoopknop", u"Gyroknapp"}},
    {"GYRO BUTTON MODE", {u"Mode du bouton du gyroscope", u"Gyro-Tastenmodus", u"Modalità pulsante del giroscopio", u"Modo del botón del giroscopio", u"Modus gyroscoopknop", u"Gyroknappsläge"}},
    {"RECENTER BUTTON", {u"Bouton de recentrage", u"Zentrieren-Taste", u"Pulsante di ricentratura", u"Botón de recentrado", u"Centreerknop", u"Centreringsknapp"}},
    {"CALIBRATE GYRO", {u"Calibrer le gyroscope", u"Gyro kalibrieren", u"Calibra il giroscopio", u"Calibrar el giroscopio", u"Gyroscoop kalibreren", u"Kalibrera gyro"}},
    {"KEEP STILL", {u"Ne bougez pas", u"Nicht bewegen", u"Non muovere", u"No lo muevas", u"Stil houden", u"Håll stilla"}},
    {"START", {u"Démarrer", u"Starten", u"Avvia", u"Iniciar", u"Starten", u"Starta"}},
    {"YAW", {u"Lacet", u"Gieren", u"Imbardata", u"Guiñada", u"Gieren", u"Gir"}},
    {"ROLL", {u"Roulis", u"Rollen", u"Rollio", u"Alabeo", u"Rollen", u"Roll"}},
    {"WORLD", {u"Monde", u"Welt", u"Mondo", u"Mundo", u"Wereld", u"Värld"}},
    {"NONE", {u"Aucun", u"Keine", u"Nessuno", u"Ninguno", u"Geen", u"Ingen"}},
    {"TOUCHPAD", {u"Pavé tactile", u"Touchpad", u"Touchpad", u"Panel táctil", u"Touchpad", u"Pekplatta"}},
    {"MISC", {u"Divers", u"Sonstige", u"Varie", u"Varios", u"Overig", u"Övrigt"}},
    {"RIGHT PADDLE", {u"Palette droite", u"Paddle rechts", u"Paletta destra", u"Palanca derecha", u"Rechterpeddel", u"Höger paddel"}},
    {"LEFT PADDLE", {u"Palette gauche", u"Paddle links", u"Paletta sinistra", u"Palanca izquierda", u"Linkerpeddel", u"Vänster paddel"}},
    {"OFF WHILE HELD", {u"Désactivé si maintenu", u"Aus beim Halten", u"Disattivato se tenuto premuto", u"Desactivado al mantener", u"Uit bij vasthouden", u"Av när den hålls"}},
    {"ON WHILE HELD", {u"Activé si maintenu", u"Ein beim Halten", u"Attivato se tenuto premuto", u"Activado al mantener", u"Aan bij vasthouden", u"På när den hålls"}},

    // keyboard and mouse actions
    {"MOVE FORWARD", {u"Avancer", u"Vorwärts bewegen", u"Avanti", u"Avanzar", u"Vooruit", u"Gå framåt"}},
    {"MOVE BACK", {u"Reculer", u"Rückwärts bewegen", u"Indietro", u"Retroceder", u"Achteruit", u"Gå bakåt"}},
    {"STRAFE LEFT", {u"Pas latéral gauche", u"Seitwärts links", u"Passo laterale a sinistra", u"Paso lateral izquierda", u"Zijwaarts links", u"Sidosteg vänster"}},
    {"STRAFE RIGHT", {u"Pas latéral droit", u"Seitwärts rechts", u"Passo laterale a destra", u"Paso lateral derecha", u"Zijwaarts rechts", u"Sidosteg höger"}},
    {"LOOK UP", {u"Regarder en haut", u"Nach oben sehen", u"Guarda in alto", u"Mirar arriba", u"Omhoog kijken", u"Titta upp"}},
    {"LOOK DOWN", {u"Regarder en bas", u"Nach unten sehen", u"Guarda in basso", u"Mirar abajo", u"Omlaag kijken", u"Titta ned"}},
    {"LOOK LEFT", {u"Regarder à gauche", u"Nach links sehen", u"Guarda a sinistra", u"Mirar a la izquierda", u"Naar links kijken", u"Titta vänster"}},
    {"LOOK RIGHT", {u"Regarder à droite", u"Nach rechts sehen", u"Guarda a destra", u"Mirar a la derecha", u"Naar rechts kijken", u"Titta höger"}},
    {"LOOK BACK", {u"Regarder derrière", u"Nach hinten sehen", u"Guarda indietro", u"Mirar atrás", u"Achterom kijken", u"Titta bakåt"}},
    {"FIRE", {u"Tirer", u"Feuern", u"Fuoco", u"Disparar", u"Vuren", u"Skjut"}},
    {"ALT FIRE", {u"Tir secondaire", u"Sekundärfeuer", u"Fuoco alternativo", u"Disparo secundario", u"Alternatief vuur", u"Alternativ eld"}},
    {"RELOAD", {u"Recharger", u"Nachladen", u"Ricarica", u"Recargar", u"Herladen", u"Ladda om"}},
    {"ACTION", {u"Action", u"Aktion", u"Azione", u"Acción", u"Actie", u"Handling"}},
    {"JUMP", {u"Sauter", u"Springen", u"Salta", u"Saltar", u"Springen", u"Hoppa"}},
    {"CROUCH", {u"S'accroupir", u"Ducken", u"Chinati", u"Agacharse", u"Bukken", u"Huka"}},
    {"WALK", {u"Marcher", u"Gehen", u"Cammina", u"Caminar", u"Lopen", u"Gå"}},
    {"ZOOM IN", {u"Zoom avant", u"Heranzoomen", u"Zoom avanti", u"Acercar", u"Inzoomen", u"Zooma in"}},
    {"ZOOM OUT", {u"Zoom arrière", u"Herauszoomen", u"Zoom indietro", u"Alejar", u"Uitzoomen", u"Zooma ut"}},
    {"NEXT WEAPON", {u"Arme suivante", u"Nächste Waffe", u"Arma successiva", u"Arma siguiente", u"Volgend wapen", u"Nästa vapen"}},
    {"PREVIOUS WEAPON", {u"Arme précédente", u"Vorherige Waffe", u"Arma precedente", u"Arma anterior", u"Vorig wapen", u"Föregående vapen"}},
    {"NEXT GADGET", {u"Gadget suivant", u"Nächstes Gadget", u"Gadget successivo", u"Gadget siguiente", u"Volgende gadget", u"Nästa pryl"}},
    {"PREVIOUS GADGET", {u"Gadget précédent", u"Vorheriges Gadget", u"Gadget precedente", u"Gadget anterior", u"Vorige gadget", u"Föregående pryl"}},
    {"USE GADGET", {u"Utiliser le gadget", u"Gadget benutzen", u"Usa gadget", u"Usar gadget", u"Gadget gebruiken", u"Använd pryl"}},
    {"VISION MODE", {u"Mode de vision", u"Sichtmodus", u"Modalità visore", u"Modo de visión", u"Zichtmodus", u"Synläge"}},
    {"PAUSE MENU", {u"Menu pause", u"Pausenmenü", u"Menu di pausa", u"Menú de pausa", u"Pauzemenu", u"Pausmeny"}},
    {"MENU BUTTON", {u"Bouton de menu", u"Menütaste", u"Pulsante menu", u"Botón de menú", u"Menuknop", u"Menyknapp"}},
    {"TOGGLE HUD", {u"Afficher / masquer l'interface", u"HUD ein / aus", u"Mostra / nascondi HUD", u"Mostrar / ocultar HUD", u"HUD aan / uit", u"Visa / dölj HUD"}},
    {"TOGGLE TEXTURE REPLACEMENT", {u"Activer / désactiver le remplacement des textures", u"Texturersetzung ein / aus", u"Attiva / disattiva sostituzione delle texture", u"Activar / desactivar reemplazo de texturas", u"Texturen vervangen aan / uit", u"Texturersättning på / av"}},
    {"SETTINGS PANEL", {u"Panneau des paramètres", u"Einstellungsmenü", u"Pannello impostazioni", u"Panel de configuración", u"Instellingenpaneel", u"Inställningspanel"}},
    {"ACCELERATE", {u"Accélérer", u"Beschleunigen", u"Accelera", u"Acelerar", u"Gas geven", u"Gasa"}},
    {"BRAKE / REVERSE", {u"Freiner / marche arrière", u"Bremsen / Rückwärts", u"Frena / Retromarcia", u"Frenar / Marcha atrás", u"Remmen / Achteruit", u"Bromsa / Backa"}},
    {"STEER LEFT", {u"Tourner à gauche", u"Links lenken", u"Sterza a sinistra", u"Girar a la izquierda", u"Links sturen", u"Styr vänster"}},
    {"STEER RIGHT", {u"Tourner à droite", u"Rechts lenken", u"Sterza a destra", u"Girar a la derecha", u"Rechts sturen", u"Styr höger"}},
    {"STEER UP", {u"Diriger vers le haut", u"Nach oben lenken", u"Sterza in alto", u"Dirigir hacia arriba", u"Omhoog sturen", u"Styr uppåt"}},
    {"STEER DOWN", {u"Diriger vers le bas", u"Nach unten lenken", u"Sterza in basso", u"Dirigir hacia abajo", u"Omlaag sturen", u"Styr nedåt"}},
    {"HANDBRAKE", {u"Frein à main", u"Handbremse", u"Freno a mano", u"Freno de mano", u"Handrem", u"Handbroms"}},
    {"CAMERA VIEW", {u"Vue caméra", u"Kameraansicht", u"Visuale", u"Vista de cámara", u"Camerabeeld", u"Kameravy"}},

    // pause menu legends
    {"Select", {u"Sélectionner", u"Auswählen", u"Seleziona", u"Seleccionar", u"Selecteren", u"Välj"}},
    {"Continue", {u"Continuer", u"Weiter", u"Continua", u"Continuar", u"Doorgaan", u"Fortsätt"}},
    {"Change key", {u"Changer la touche", u"Taste ändern", u"Cambia tasto", u"Cambiar tecla", u"Toets wijzigen", u"Byt tangent"}},
    {"Change", {u"Modifier", u"Ändern", u"Cambia", u"Cambiar", u"Wijzigen", u"Ändra"}},
    {"Scroll", {u"Défiler", u"Blättern", u"Scorri", u"Desplazar", u"Scrollen", u"Bläddra"}},
    {"Cancel", {u"Annuler", u"Abbrechen", u"Annulla", u"Cancelar", u"Annuleren", u"Avbryt"}},

    // QUIT on the main menu
    {"Quit", {u"Quitter", u"Beenden", u"Esci", u"Salir", u"Afsluiten", u"Avsluta"}},
    {"Exit the game.", {u"Quitter le jeu.", u"Spiel beenden.", u"Esci dal gioco.", u"Salir del juego.", u"Het spel afsluiten.", u"Avsluta spelet."}},
    {"Are you sure you want to exit?", {u"Voulez-vous vraiment quitter ?", u"Möchtest du das Spiel wirklich beenden?", u"Vuoi davvero uscire?", u"¿Seguro que quieres salir?", u"Weet je zeker dat je wilt afsluiten?", u"Är du säker på att du vill avsluta?"}},
};

// the column of kEntries for the language this run started with, -1 for English
int Column() {
    static const int column = [] {
        switch (game::BootLanguage()) {
            case game::kFrench: return 0;
            case game::kGerman: return 1;
            case game::kItalian: return 2;
            case game::kSpanish: return 3;
            case game::kDutch: return 4;
            case game::kSwedish: return 5;
            default: return -1;
        }
    }();
    return column;
}

bool Same(std::string_view a, std::string_view b) {
    auto upper = [](char c) { return std::toupper(uint8_t(c)); };
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) {
               return upper(x) == upper(y);
           });
}

const char16_t* Translation(std::string_view english) {
    for (const Entry& e : kEntries)
        if (Same(e.english, english)) return e.text[Column()].data();
    return nullptr;
}

bool WordChar(char c) { return std::isalnum(uint8_t(c)) != 0; }

// the longest phrase with a translation at each word start
std::u16string Translate(std::string_view text) {
    std::u16string out;
    for (size_t i = 0; i < text.size();) {
        const char16_t* found = nullptr;
        size_t end = text.size();
        if (!i || !WordChar(text[i - 1]))
            for (; end > i; end--) {
                bool word_end = end == text.size() || !WordChar(text[end]);
                if (word_end && (found = Translation(text.substr(i, end - i)))) break;
            }
        if (!found) {
            out += char16_t(uint8_t(text[i++]));
            continue;
        }
        out += found;
        i = end;
    }
    return out;
}

// Latin-1 letters keep their place 0x20 below their lower case, as ASCII's; ß has no capital
char16_t Upper(char16_t c) {
    bool ascii = c >= u'a' && c <= u'z';
    bool latin = c >= 0xE0 && c <= 0xFE && c != 0xF7;
    return ascii || latin ? char16_t(c - 0x20) : c;
}

std::u16string Localized(std::string_view english) {
    std::u16string text = Translate(english);
    if (std::none_of(english.begin(), english.end(), [](char c) { return std::islower(uint8_t(c)); }))
        std::transform(text.begin(), text.end(), text.begin(), Upper);
    return text;
}

std::string Latin1(std::string_view english) {
    std::string out;
    for (char16_t c : Localized(english)) out += c <= 0xFF ? char(c) : '?';
    return out;
}

std::string Utf8(std::string_view english) {
    std::string out;
    for (char16_t c : Localized(english)) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | c >> 6);
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xE0 | c >> 12);
            out += char(0x80 | (c >> 6 & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// the panel asks for every row each frame
using Cache = std::unordered_map<std::string, std::string>;
std::string Cached(Cache& cache, std::string_view english, std::string (*make)(std::string_view)) {
    static std::mutex lock;
    std::lock_guard hold(lock);
    auto [it, added] = cache.try_emplace(std::string(english));
    if (added) it->second = make(english);
    return it->second;
}

}  // namespace

std::string game::GameText(std::string_view english) {
    if (Column() < 0) return std::string(english);
    static Cache cache;
    return Cached(cache, english, Latin1);
}

std::string game::PanelText(std::string_view english) {
    if (Column() < 0) return std::string(english);
    static Cache cache;
    return Cached(cache, english, Utf8);
}
