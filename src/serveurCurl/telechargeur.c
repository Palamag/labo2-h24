/* TP2 Hiver 2024 
 * Code source fourni
 * Marc-Andre Gardner
 */

#include "telechargeur.h"

// Adresse du serveur
const char baseUrl[] = "http://wcours.gel.ulaval.ca/GIF3004/labo2/";


// Fonction utilisée à l'interne, vous n'avez pas à l'utiliser directement
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp){
    size_t realsize = size * nmemb;
    struct MemoryStructCurl *mem = (struct MemoryStructCurl *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}


void executerRequete(int pipeFd, char* reqBuffer){
    // Execute une requete dont la description est contenue dans reqBuffer
    // Cette fonction s'execute dans un _autre_ processus
    // et utilise libcurl
    // Voir https://curl.haxx.se/libcurl/c/getinmemory.html pour l'exemple
    // utilise ici
    // Copyright (C) 1998 - 2015, Daniel Stenberg, <daniel@haxx.se>, et al.
    //
    // Modifie pour fonctionner avec l'architecture du TP2

    int offsetFichier = 0;
    struct msgReq req;
    const char index[] = "index.txt";       // Nom par defaut du fichier listant le contenu du repertoire
    
    // On cree l'URL
    memcpy(&req, reqBuffer, sizeof(req));
    size_t allocsize = (req.type == REQ_LIST) ? (sizeof(index) + sizeof(baseUrl)) : (req.sizePayload + sizeof(baseUrl));
    if(req.type == REQ_READ && strchr((char *)(reqBuffer + sizeof(req)), '/')){
        // Si on recoit une requete avec "/" au debut du nom (ex. "/fichier.cpp"),
        // on ne veut pas repeter le / qui est deja present dans baseUrl.
        // Donc on alloue 1 caractere de moins, et on memorise dans offsetFichier
        // le fait qu'il faut partir du _2e_ caractere et non du premier.
        allocsize -= 1;
        offsetFichier = 1;
    }
    // On ajoute 1 pour tenir compte du null character terminant la chaine de caracteres
    char* fname = malloc(allocsize + 1);

    if(req.type == REQ_LIST){
        strncpy(fname, baseUrl, allocsize);
        strncat(fname, index, allocsize);
    }
    else if(req.type == REQ_READ){
        strncpy(fname, baseUrl, allocsize);
        strncat(fname, reqBuffer + sizeof(req) + offsetFichier, allocsize);
    }
    else{
        printf("Requete invalide!\n");
        exit(-1);
    }
    if(VERBOSE)
        printf("Envoi de la requete pour l'URL : %s\n", fname);

    CURL *curl_handle;
    CURLcode res;

    struct MemoryStructCurl chunk;
    chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    chunk.size = 0;    /* no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);

    /* init the curl session */
    curl_handle = curl_easy_init();
    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, fname);
    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    /* some servers don't like requests that are made without a user-agent
     field, so we provide one */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    // Important : il faut detecter les 404!
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1);

    /* get it! */
    res = curl_easy_perform(curl_handle);

    /* check for errors */
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
        long http_code = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
        fprintf(stderr, "HTTP code returned: %ld\n", http_code);
        size_t buferror = 0;
        write(pipeFd, (char*)&buferror, sizeof(size_t));
    }
    else {
         // Nous envoyons d'abord la taille du buffer
         // Il faut convertir l'entier qui le contient en char*
         unsigned int c, writtenBytes = 0;
         c = write(pipeFd, (char*)&(chunk.size), sizeof(size_t));
         if(VERBOSE)
            printf("Ecriture sur le pipe, %i octets ecrits\n", c);

         while(writtenBytes < chunk.size){
             // Le buffer du pipe peut ne pas etre assez gros pour tout contenir d'un coup!
             c = write(pipeFd, chunk.memory + writtenBytes, chunk.size - writtenBytes);
             if(VERBOSE)
                printf("Ecriture sur le pipe, %i octets ecrits sur %i\n", c, chunk.size);
             writtenBytes += c;
         }
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);

    free(chunk.memory);
    free(fname);

    /* we're done with libcurl, so clean it up */
    curl_global_cleanup();

    // Fermeture du pipe, avant de quitter
    close(pipeFd);
}
