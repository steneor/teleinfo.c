/*       teleinfo.c										*/
/* Version pour PC testé susr ubuntu, RPI sous raspbian										*/
/* Lecture données Téléinfo et enregistre données sur base mysql si ok sinon dans fichier csv.	*/
/* Connexion par le port série du (Console désactivée dans inittab.)				*/
/* Vérification checksum données téléinfo et boucle de 3 essais si erreurs.				*/
/* 									*/

/*
Paramètres à adapter:
- Port série à modifier en conséquence avec SERIALPORT.
- Nombre de valeurs à relever: nb_valeurs + tableaux "etiquettes" et "poschecksum" à modifier selon abonnement (ici triphasé heures creuses).
- Paramètres Mysql (Serveur, Base, table et login/password)
- Autorisé le serveur MySql à accepter les connexions distantes pour le Wrt54gl.

Compilation PC:
- gcc -Wall teleinfoserial_mysql.c -o teleinfoserial_mysql -lmysqlclient

Compilation wrt54gl:
- avec le SDK (OpenWrt-SDK-Linux).

Exemple affichage

----- 2012-05-12 11:09:25 -----
ADCO='020xxxxxxxxx'
OPTARIF='BASE'
ISOUSC='30'
BASE='015284865'
PTEC='TH..'
IINST='003'
IMAX='012'
PAPP='00760'
MOTDETAT='000000'
 */

//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <termios.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <mysql/mysql.h>

// Define port serie
#define BAUDRATE B1200
char serialport[32] ; //MODIF EV

// Define mysql
char mysql_host[128];
char mysql_db[32];
char mysql_table[32];
char mysql_login[32];
char mysql_pwd[32];

// Fichier local au Wrt4gl/PC + fichier trame pour debug.
#define DATACSV "/tmp/teleinfosql.csv"
#define TRAMELOG "/tmp/teleinfotrame."

// Active mode debug.
//#define DEBUG

//-----------------------------------------------------------------------------
// Déclaration pour gérer les options de la ligne de commande
int errflg = 0;
int cflag = 0;

// Déclaration pour syslog
char ident_syslog [32] = "teleinfo_"   ; //La chaine pointé par ident_syslog sera ajouté à chaque message

// Déclaration pour le port série.
int             fdserial ;
struct termios  termiosteleinfo ;

// Déclaration pour les données.
char ch[2] = " ";       //init de ch avec espace par  exemple
char car_prec ;
char message[512] ;
char* match;
int id, nb_valeurs ;  //nb de valeurs du tableau etiquettes
char datateleinfo[512] ;

char OPTARIF_BASE[13] = "OPTARIF BASE" ; // Si dans la trame il existe "OPTARIF BASE" on la remplce par "OPTARIF BAES" avec CHEKSUM OK
char OPTARIF_BAES[13] = "OPTARIF BAES" ; // cela permet de différencier l'index qui est repéré par "BASE"//BASE
char trame[8]; //type de trame attendu pv ou mono ou tri

// Constantes/Variables à changées suivant abonnement, Nombre de valeurs, voir tableau "etiquettes", 20 pour abonnement tri heures creuse.
//#define NB_VALEURS_PV 9 // 9 valeurs pour PV


// constitution de la trame si option souscrite BASE (pour les compteurs de production PV)
// chaque ligne est constituée de etiquettes  +  valeurs  +  cheksum
char etiquettesPV[9][16] = {"ADCO", "OPTARIF", "ISOUSC", "BASE", "PTEC",  "IINST", "IMAX",  "PAPP",  "MOTDETAT"};
char etiquettes[20][16] = {"ADCO", "OPTARIF", "ISOUSC", "HCHP", "HCHC", "PTEC", "IINST", "IINST2", "IINST3", "IMAX", "IMAX2", "IMAX3", "PMAX", "PAPP", "HHPHC", "MOTDETAT", "PPOT", "ADIR", "ADIR2" ,"ADIR3"} ;


// Fin Constantes/variables à changées suivant abonnement.

char 	valeurs[20][18] ;  // Nbre de valeurs de 0 à 18 = 19
char 	checksum[255] ;
int 	res ;
int	no_essais = 1 ;
int	nb_essais = 3 ;
int	erreur_checksum = 0 ;

// Déclaration pour la date.
time_t 		td;
struct 	tm 	*dc;
char		sdate[12];
char		sheure[10];
char		timestamp[11];

/*------------------------------------------------------------------------------*/
/* Message d'aide MODIF EV	                                     				*/
/*------------------------------------------------------------------------------*/
void aide(void)
{
    printf("teleinfo version 1.1 du 25/02/2013 modifié par EV\n");
    printf("GNU Public License v2.0 - http://enersol.free.fr/teleinfo\n");
    printf("Lit la trame de teleinfo envoyé par le compteur\n\r");

    printf("Usage: teleinfo [-s -t -x -y -z -w]\n");
    printf("                -s /dev/ttyS0                 Choix du port serie ou USB\n");
    printf("                -t pv                         Type de compteur: pv mono tri\n");
    printf("                -v mysql_host                 Host mysql\n");
    printf("                -w mysql_db                   Base de donnée mysql\n");
    printf("                -x mysql_table                table mysql à utilisée\n");
    printf("                -y mysql_login                Login mysql\n");
    printf("                -w mysql_pwd                  Mot de passe mysql \n");

    printf("Exemple: teleinfo -s /dev/ttyUSB0 -t mono -v localhost -w bd_teleinfo -x table -y login -z pwd\n\n");

    printf("     Si l'argument -v est absent, alors la sortie de toutes les informations\n");
    printf("     ce fait sur la console.\n");
    printf("     Exemple 1: ./teleinfo -s /dev/ttyUSB0 -t pv\n");
    printf("     '1338897198','2012-06-05','13:53:18','020123456789','BAES','30','015489.690','TH','008','012','01830','000000'\n\n");

    printf("   -s: Les ports de communications, peuvent être /dev/ttyS0, /dev/ttyS1, /dev/ttyS2,\n");
    printf("       /dev/ttyUSB0, /dev/ttyUSB1, etc ...\n");
    printf("   -t: Les types de compteurs qui emettent une trame de téléinfo connue sont :\n");
    printf("           pv     pour les compteurs de production photovoltaique\n");
    printf("           mono   pour les compteurs monophasé heures creuses et heures pleines\n");
    printf("           tri    pour les compteurs triphasé heures creuses et heures pleines\n");
    printf("   -u -v -w -x -y -z : Informations pour se connecter à la base de données\n");
    //printf("     ");
    //printf("     ");
    //printf("     ");



}

/*------------------------------------------------------------------------------*/
/* Init port rs232							                                    */
/*------------------------------------------------------------------------------*/
int initserie(void)
// Mode Non-Canonical Input Processing, Attend 1 caractère ou time-out(avec VMIN et VTIME).
{
    int device ;

    // Ouverture de la liaison serie (Nouvelle version de config.)
    if ( (device=open(serialport, O_RDWR | O_NOCTTY)) == -1 )
    {
        syslog(LOG_ERR, "Erreur ouverture du port serie %s !", serialport);
        exit(1) ;
    }

    tcgetattr(device,&termiosteleinfo) ;				// Lecture des parametres courants.

    cfsetispeed(&termiosteleinfo, BAUDRATE) ;			// Configure le débit en entrée/sortie.
    cfsetospeed(&termiosteleinfo, BAUDRATE) ;

    termiosteleinfo.c_cflag |= (CLOCAL | CREAD) ;			// Active réception et mode local.

    // Format série "7E1"
    termiosteleinfo.c_cflag |= PARENB  ;				// Active 7 bits de donnees avec parite pair.
    termiosteleinfo.c_cflag &= ~PARODD ;
    termiosteleinfo.c_cflag &= ~CSTOPB ;
    termiosteleinfo.c_cflag &= ~CSIZE ;
    termiosteleinfo.c_cflag |= CS7 ;

    termiosteleinfo.c_iflag |= (INPCK | ISTRIP) ;			// Mode de control de parité.

    termiosteleinfo.c_cflag &= ~CRTSCTS ;				// Désactive control de flux matériel.

    termiosteleinfo.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG) ;	// Mode non-canonique (mode raw) sans echo.

    termiosteleinfo.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL) ;	// Désactive control de flux logiciel, conversion 0xOD en 0x0A.

    termiosteleinfo.c_oflag &= ~OPOST ;				// Pas de mode de sortie particulier (mode raw).

    termiosteleinfo.c_cc[VTIME] = 80 ;  				// time-out à ~8s.
    termiosteleinfo.c_cc[VMIN]  = 0 ;   				// 1 car. attendu.

    tcflush(device, TCIFLUSH) ;					// Efface les données reçues mais non lues.
    tcsetattr(device,TCSANOW,&termiosteleinfo) ;			// Sauvegarde des nouveaux parametres
    return device ;
}

/*------------------------------------------------------------------------------*/
/* Lecture données téléinfo sur port série				                        */
/*------------------------------------------------------------------------------*/
void LiTrameSerie(int device)
{
// (0d 03 02 0a => Code fin et début trame)
    tcflush(device, TCIFLUSH) ;			// Efface les données non lus en entrée.
    message[0]='\0' ;
    memset(valeurs, 0x00, sizeof(valeurs)) ;

    do
    {
        car_prec = ch[0] ;
        res = read(device, ch, 1) ;
        if (! res)
        {
            syslog(LOG_ERR, "Erreur pas de réception début données Téléinfo !\n") ;
            close(device);
            exit(1) ;
        }
    }
    while ( ! (ch[0] == 0x02 && car_prec == 0x03) ) ;	// Attend code fin suivi de début trame téléinfo .

    do
    {
        res = read(device, ch, 1) ;
        if (! res)
        {
            syslog(LOG_ERR, "Erreur pas de réception fin données Téléinfo !\n") ;
            close(device);
            exit(1) ;
        }
        ch[1] ='\0' ;
        strcat(message, ch) ;
    }
    while (ch[0] != 0x03) ;				// Attend code fin trame téléinfo.
//printf ("1),%s\n\n",message);
#ifdef DEBUG
    printf ("fonction litTrameSerie 1:%s\n",message)	 ;
#endif
// Si dans la trame il existe "OPTARIF BASE" on la remplce par "OPTARIF BAES"
// cela permet de différencier l'index qui est repéré par "BASE"
    if (((match = strstr(message, OPTARIF_BASE)  )) != NULL) //renvoie un pointeur sur le début de la sous-chaîne
    {
        memcpy (match,OPTARIF_BAES,12);     // remplace la sous chaine "OPTARIF BASE" par "OPTARIF BAES"
    }
//printf ("2),%s\n\n",message);
#ifdef DEBUG
    printf ("fonction litTrameSerie 2:%s\n",message)	 ;
#endif
}

/*------------------------------------------------------------------------------*/
/* Test checksum d'un message (Return 1 si checkum ok)			                */
/*------------------------------------------------------------------------------*/
int checksum_ok(char *etiquette, char *valeur, char checksum)
{
    unsigned char sum = 32 ;		// Somme des codes ASCII du message + un espace
    int i ;

    for (i=0; i < strlen(etiquette); i++) sum = sum + etiquette[i] ;
    for (i=0; i < strlen(valeur); i++) sum = sum + valeur[i] ;
    sum = (sum & 63) + 32 ;
    if ( sum == checksum) return 1 ;	// Return 1 si checkum ok.
#ifdef DEBUG
    syslog(LOG_INFO, "Checksum lu:%02x   calculé:%02x", checksum, sum) ;
#endif
    return 0 ;
}

/*------------------------------------------------------------------------------*/
/* Recherche valeurs des étiquettes de la liste. compteur conso mono ou tri     */
/*------------------------------------------------------------------------------*/
int LitValEtiquettes()
{
    int id ;
    erreur_checksum = 0 ;

    for (id=0; id<nb_valeurs; id++)
    {
        if ( (match = strstr(message, etiquettes[id])) != NULL)
        {
            sscanf(match, "%s %s %s", etiquettes[id], valeurs[id], checksum) ;
            if ( strlen(checksum) > 1 ) checksum[0]=' ' ;	// sscanf ne peux lire le checksum à 0x20 (espace), si longueur checksum > 1 donc c'est un espace.
            if ( ! checksum_ok(etiquettes[id], valeurs[id], checksum[0]) )
            {
                syslog(LOG_ERR, "Donnees teleinfo [%s] corrompues (essai %d) !\n", etiquettes[id], no_essais) ;
                erreur_checksum = 1 ;
                return 0 ;
            }
        }
    }
    // Remplace chaine "HP.." ou "HC.." par "HP ou "HC".
    valeurs[5][2] = '\0' ;
#ifdef DEBUG
    printf("----------------------\n") ;
    for (id=0; id<nb_valeurs; id++) printf("%s='%s'\n", etiquettes[id], valeurs[id]) ;
#endif
    //index1 = atol (valeurs[3]);
    //printf ("index=%ld \n",index1);
    if (atol (valeurs[3]) == 0  || atol (valeurs[4])== 0)  // test si les index HP HC = zero
        return 0 ;
    else
        return 1 ;
}

/*------------------------------------------------------------------------------*/
/* Recherche valeurs des étiquettes de la liste. compteur PV                    */
/*------------------------------------------------------------------------------*/
int LitValEtiquettesPV()
{
    int id ;
    erreur_checksum = 0 ;

    for (id=0; id<nb_valeurs; id++)
    {
        if ( (match = strstr(message, etiquettesPV[id])) != NULL)
        {
            sscanf(match, "%s %s %s", etiquettesPV[id], valeurs[id], checksum) ;

            //	printf ("%s %s",etiquettesPV[id], valeurs[id]);


            if ( strlen(checksum) > 1 ) checksum[0]=' ' ;	// sscanf ne peux lire le checksum à 0x20 (espace), si longueur checksum > 1 donc c'est un espace.
            if ( ! checksum_ok(etiquettesPV[id], valeurs[id], checksum[0]) )
            {
                syslog(LOG_ERR, "Donnees teleinfo [%s] corrompues (essai %d) !\n", etiquettesPV[id], no_essais) ;
                erreur_checksum = 1 ;
                return 0 ;
            }
        }
    }
    // Remplace la chaine "HP.." ou "HC.." ou "HT.. "par "HP ou "HC" ou "HT".  (chaine pointée par PTEC)
    valeurs[4][2] = '\0' ;
#ifdef DEBUG
    printf("----------------------\n") ;
    for (id=0; id<nb_valeurs; id++) printf("%s='%s'\n", etiquettesPV[id], valeurs[id]) ;
    printf("nb_valeurs:%d \n",nb_valeurs);
#endif
    if (atol (valeurs[3]) == 0 )  // test si ' index BASE = zero
        return 0 ;
    else
        return 1 ;
}

/*------------------------------------------------------------------------------*/
/* Ecrit les données teleinfo dans base mysql					                */
/*------------------------------------------------------------------------------*/
int writemysqlteleinfo(char data[])
{
    MYSQL mysql ;
    char query[255] ;

    /* INIT MYSQL AND CONNECT ----------------------------------------------------*/
    if(!mysql_init(&mysql))
    {
        syslog(LOG_ERR, "Erreur: Initialisation MySQL impossible !") ;
        return 0 ;
    }
    if(!mysql_real_connect(&mysql, mysql_host, mysql_login,	mysql_pwd, mysql_db, 0, NULL, 0))
    {
        syslog(LOG_ERR, "Erreur connection %d: %s \n", mysql_errno(&mysql), mysql_error(&mysql));
        return 0 ;
    }

    sprintf(query, "INSERT INTO %s VALUES (%s)", mysql_table, data);

    if(mysql_query(&mysql, query))
    {
        syslog(LOG_ERR, "Erreur INSERT %d: \%s \n", mysql_errno(&mysql), mysql_error(&mysql));
        mysql_close(&mysql);
        return 0 ;
    }
#ifdef DEBUG
    else syslog(LOG_INFO, "Requete MySql ok.") ;
#endif
    mysql_close(&mysql);
    return 1 ;
}

/*------------------------------------------------------------------------------*/
/* Ecrit les données teleinfo dans fichier DATACSV			                    */
/*------------------------------------------------------------------------------*/
void writecsvteleinfo(char data[])
{
    /* Ouverture fichier csv */
    FILE *datateleinfo ;
    if ((datateleinfo = fopen(DATACSV, "a")) == NULL)
    {
        syslog(LOG_ERR, "Erreur ouverture fichier teleinfo %s !", DATACSV) ;
        exit(1);
    }
    fprintf(datateleinfo, "%s\n", data) ;
    fclose(datateleinfo) ;
}

#ifdef DEBUG
/*------------------------------------------------------------------------------*/
/* Ecrit la trame teleinfo dans fichier si erreur (pour debugger)	            */
/*------------------------------------------------------------------------------*/
void writetrameteleinfo(char trame[], char ts[])
{
    char nomfichier[255] = TRAMELOG ;
    strcat(nomfichier, ts) ;
    FILE *teleinfotrame ;
    if ((teleinfotrame = fopen(nomfichier, "w")) == NULL)
    {
        syslog(LOG_ERR, "Erreur ouverture fichier teleinfotrame %s !", nomfichier) ;
        exit(1);
    }
    fprintf(teleinfotrame, "%s", trame) ;
    fclose(teleinfotrame) ;
}
#endif

/*------------------------------------------------------------------------------*/
/* Transforme la valeur de l'index du style 011684366  en 	011684,366       	*/
/*------------------------------------------------------------------------------*/
void format_index(char valeurs[])
{
    valeurs[10] = 0x0;                  //nul en fin de  chaine
    valeurs[9] = valeurs[8];
    valeurs[8] = valeurs[7];
    valeurs[7] = valeurs[6];
    valeurs[6] = '.';                   //séparateur point

}

/*------------------------------------------------------------------------------*/
/* Main									                                        */
/*------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    if( argc <= 1 )
    {
        fprintf(stderr,"Erreur! pas assez d'arguments.\n\n");
        aide();
        return -1;
    }
    int opt ;
    while ((opt = getopt(argc , argv, "s:t:v:w:x:y:z:")) != -1)
    {
        switch (opt)
        {
        case 's':
            strncpy( serialport, optarg, sizeof(serialport) - 1 );
            break;
        case 't':
            strncpy( trame, optarg, sizeof(trame) - 1 );
            if (!strcmp (trame,"pv"))
            {
                nb_valeurs = 9;
            }
            if (!strcmp (trame,"mono"))
            {
                nb_valeurs = 20;
            }
            //printf ("trame:%s\n",trame);
            break;
        case 'v':
            strncpy( mysql_host, optarg, sizeof(mysql_host) - 1 );
            break;
        case 'w':
            strncpy( mysql_db, optarg, sizeof(mysql_db) - 1 );
            break;
        case 'x':
            strncpy( mysql_table, optarg, sizeof(mysql_table) - 1 );
            break;
        case 'y':
            strncpy( mysql_login, optarg, sizeof(mysql_login) - 1 );
            break;
        case 'z':
            strncpy( mysql_pwd, optarg, sizeof(mysql_pwd) - 1 );
            break;
        case '?':
            errflg++;
            break;
        }
        if (errflg)
            aide();
    }

    strcat (ident_syslog,serialport);
    openlog( ident_syslog , LOG_PID, LOG_USER) ;

    fdserial = initserie() ;

    do
    {
        // Lit trame téléinfo.
        LiTrameSerie(fdserial) ;

        time(&td) ;                                     //Lit date/heure système.
        dc = localtime(&td) ;
        strftime(sdate,sizeof sdate,"%Y-%m-%d",dc);
        strftime(sheure,sizeof sdate,"%H:%M:%S",dc);
        strftime(timestamp,sizeof timestamp,"%s",dc);

#ifdef DEBUG
        writetrameteleinfo(message, timestamp) ;	// Enregistre trame en mode debug.
#endif

        if (!strcmp (trame,"pv"))
        {
            if ( LitValEtiquettesPV() )  			// Lit valeurs des étiquettes de la liste.
            {
                if (mysql_host[0]== 0x00)
                {
                    format_index(valeurs[3]);    // formatage de la valeur de l'index
                    sprintf(datateleinfo,"'%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s'", timestamp, sdate, sheure, valeurs[0], valeurs[1], valeurs[2], valeurs[3], valeurs[4], valeurs[5], valeurs[6], valeurs[7], valeurs[8]) ;
                    printf ("%s\n",datateleinfo);

                }
                else
                {

                    sprintf(datateleinfo,"'%s','%s','%s','%s','%s'", timestamp, sdate, sheure, valeurs[3], valeurs[7]) ;
                    if (! writemysqlteleinfo(datateleinfo) ) writecsvteleinfo(datateleinfo) ;		// Si écriture dans base MySql KO, écriture dans fichier csv.
                }
            }
        }
        if (!strcmp (trame,"mono"))
        {
            if ( LitValEtiquettes() ) 			// Lit valeurs des étiquettes de la liste.

            {
                if (mysql_host[0]== 0x00)
                {
                    format_index(valeurs[3]);    // formatage de la valeur de l'index
                    format_index(valeurs[4]);    // formatage de la valeur de l'index
                    sprintf(datateleinfo,"'%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s'", timestamp, sdate, sheure, valeurs[0], valeurs[1], valeurs[2], valeurs[3], valeurs[4], valeurs[5], valeurs[6], valeurs[7], valeurs[8]) ;
                    printf ("%s\n",datateleinfo);

                }
                else
                {
                    sprintf(datateleinfo,"'%s','%s','%s','%s','%s','%s','%s'", timestamp, sdate, sheure,valeurs[3], valeurs[4], valeurs[5],valeurs[13]) ;
                    if (! writemysqlteleinfo(datateleinfo) ) writecsvteleinfo(datateleinfo) ;		// Si écriture dans base MySql KO, écriture dans fichier csv.

                    //DepasseCapacite() ; 			// Test si etiquette dépassement intensité (log l'information seulement).
                }
            }
        }
#ifdef DEBUG
        else writetrameteleinfo(message, timestamp) ;	// Si erreur checksum enregistre trame.
#endif
        no_essais++ ;
    }
    while ( (erreur_checksum) && (no_essais <= nb_essais) ) ;

    close(fdserial) ;
    closelog() ;
    exit(0) ;
}
