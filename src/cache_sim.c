#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

/* -----------------------------
   Strukture cache memorije
------------------------------*/

// Jedna cache linija: valid bit, tag i LRU brojač
typedef struct {
    uint8_t valid;      // označava je li linija zauzeta
    uint64_t tag;       // tag dijela adrese
    uint32_t lru;       // LRU brojač 
} CacheLine;

// Jedan set (skup) u cache-u: ima više linija (ovisno o asocijativnosti)
typedef struct {
    CacheLine *lines;   // pokazivač na linije unutar seta
    uint32_t ways;      // broj linija po setu (asocijativnost)
} CacheSet;

// Struktura cijelog cache-a
typedef struct {
    CacheSet *sets;         // svi setovi
    uint32_t num_sets;      // broj setova
    uint32_t ways;          // asocijativnost (broj linija u setu)
    uint32_t block_size;    // veličina bloka (B)
    uint32_t cache_size;    // ukupna veličina cache-a (B)
    uint32_t index_mask, index_shift, offset_mask; // maske i pomaci za dekodiranje adrese
    uint64_t accesses, hits, misses;               // statistika
} Cache;

/* -----------------------------
   Pomoćne funkcije
------------------------------*/

// Provjera je li broj potencija dvojke
static int is_power_of_two(uint32_t x) { return x && ((x & (x-1)) == 0); }

// Funkcija za ispis greške i prekid programa
static void die(const char *msg) {
    fprintf(stderr, "Greška: %s\n", msg);
    exit(1);
}

// Parsiranje adrese iz trace linije (može biti hex ili decimalno, s prefiksom R/W)
static uint64_t parse_address(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++; // preskoči praznine
    if (*s=='R'||*s=='W'||*s=='r'||*s=='w') {     // ako linija sadrži R/W
        while(*s && !isspace((unsigned char)*s)) s++;
        while(*s && isspace((unsigned char)*s)) s++;
    }
    uint64_t val = 0;
    if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) // hex format
        sscanf(s, "%lx", (unsigned long*)&val);
    else                                     // decimalni format
        sscanf(s, "%lu", (unsigned long*)&val);
    return val;
}

/* -----------------------------
   Cache inicijalizacija
------------------------------*/

// Izračunavanje maski i pomaka za dohvat offseta, indexa i taga iz adrese
static void cache_compute_masks(Cache *c) {
    uint32_t off_bits=0, idx_bits=0;
    for(uint32_t b=c->block_size;b>1;b>>=1) off_bits++;
    for(uint32_t n=c->num_sets;n>1;n>>=1) idx_bits++;
    c->offset_mask=(1u<<off_bits)-1u; // maska za offset
    c->index_mask=(1u<<idx_bits)-1u;  // maska za index
    c->index_shift=off_bits;          // index kreće nakon offseta
}

// Kreiranje cache memorije
static Cache cache_create(uint32_t cache_size,uint32_t block_size,uint32_t ways){
    // Validacija parametara
    if(!is_power_of_two(cache_size)) die("cache_size mora biti potencija od 2");
    if(!is_power_of_two(block_size)) die("block_size mora biti potencija od 2");
    if(block_size>cache_size) die("block_size ne smije biti veći od cache_size");
    if(ways==0) die("assoc (ways) mora biti >=1");
    if(cache_size%(block_size*ways)!=0) die("cache_size mora biti djeljiv s block_size*ways");

    // Inicijalizacija strukture
    Cache c;
    memset(&c,0,sizeof(c));
    c.cache_size=cache_size;
    c.block_size=block_size;
    c.ways=ways;
    c.num_sets=cache_size/(block_size*ways);

    // Alokacija setova
    c.sets=(CacheSet*)calloc(c.num_sets,sizeof(CacheSet));
    if(!c.sets) die("Nedovoljno memorije za setove");

    // Alokacija linija unutar svakog seta
    for(uint32_t i=0;i<c.num_sets;i++){
        c.sets[i].ways=ways;
        c.sets[i].lines=(CacheLine*)calloc(ways,sizeof(CacheLine));
        if(!c.sets[i].lines) die("Nedovoljno memorije za linije");
    }

    cache_compute_masks(&c);
    return c;
}

// Oslobađanje memorije
static void cache_free(Cache *c){
    if(!c || !c->sets) return;
    for(uint32_t i=0;i<c->num_sets;i++) free(c->sets[i].lines);
    free(c->sets);
    memset(c,0,sizeof(*c));
}

/* -----------------------------
   LRU (Least Recently Used)
------------------------------*/

// Ažuriranje LRU nakon pogodaka
static void lru_touch(CacheSet *set,uint32_t hit_way){
    uint32_t old=set->lines[hit_way].lru;
    for(uint32_t w=0;w<set->ways;w++){
        if(w==hit_way) set->lines[w].lru=0; // ovaj je najnoviji
        else if(set->lines[w].valid && set->lines[w].lru<=old) set->lines[w].lru++;
    }
}

// Odabir linije koja će biti izbačena (žrtva)
static uint32_t lru_victim(CacheSet *set){
    // prvo tražimo praznu liniju
    for(uint32_t w=0;w<set->ways;w++) if(!set->lines[w].valid) return w;
    // inače vraćamo onu s najvećim LRU (najstarija)
    uint32_t victim=0;
    for(uint32_t w=1;w<set->ways;w++) if(set->lines[w].lru>set->lines[victim].lru) victim=w;
    return victim;
}

/* -----------------------------
   Cache pristup
------------------------------*/

// Pristup adresi: vraća 1 ako je HIT, 0 ako je MISS
static int cache_access(Cache *c,uint64_t addr){
    c->accesses++; // brojimo pristup
    uint64_t block_addr=addr/c->block_size;                  // računamo adresu bloka
    uint32_t index=(uint32_t)(block_addr & c->index_mask);   // index seta
    uint64_t tag=block_addr>>(c->index_shift);               // tag adrese

    CacheSet *set=&c->sets[index];

    // Provjera postoji li blok u setu
    for(uint32_t w=0;w<set->ways;w++){
        CacheLine *line=&set->lines[w];
        if(line->valid && line->tag==tag){
            c->hits++;
            lru_touch(set,w); // ažuriranje LRU
            return 1; // HIT
        }
    }

    // MISS → ubacujemo novi blok
    c->misses++;
    uint32_t v=lru_victim(set);
    set->lines[v].valid=1;
    set->lines[v].tag=tag;
    for(uint32_t w=0;w<set->ways;w++) if(set->lines[w].valid && w!=v) set->lines[w].lru++;
    set->lines[v].lru=0;

    return 0;
}

/* -----------------------------
   Ispis statistike
------------------------------*/
static void print_stats(const Cache *c){
    double hit_rate=(c->accesses?(double)c->hits/(double)c->accesses:0.0)*100.0;
    double miss_rate=(c->accesses?(double)c->misses/(double)c->accesses:0.0)*100.0;

    printf("\n--- REZULTATI ---\n");
    printf("Cache size     : %u B\n",c->cache_size);
    printf("Block size     : %u B\n",c->block_size);
    printf("Asocijativnost : %u-way (%s)\n",c->ways,(c->ways==1?"direct-mapped":"set-associative"));
    printf("Broj setova    : %u\n",c->num_sets);
    printf("Pristupa       : %llu\n",(unsigned long long)c->accesses);
    printf("Pogodaka       : %llu\n",(unsigned long long)c->hits);
    printf("Promašaja      : %llu\n",(unsigned long long)c->misses);
    printf("Hit rate       : %.2f %%\n",hit_rate);
    printf("Miss rate      : %.2f %%\n",miss_rate);

    // Rezultati se upisuju i u CSV za kasniju analizu
    FILE *csv=fopen("cache_results.csv","a");
    if(csv){
        fprintf(csv,"%u,%u,%u,%llu,%llu,%.2f,%.2f\n",
                c->cache_size,c->block_size,c->ways,
                (unsigned long long)c->hits,
                (unsigned long long)c->misses,
                hit_rate,miss_rate);
        fclose(csv);
    }
}

/* -----------------------------
   Trace simulacija
------------------------------*/

// Čitanje trace datoteke i simulacija pristupa
static uint64_t run_trace(Cache *c,const char *trace_path){
    FILE *f=fopen(trace_path,"r");
    if(!f) die("Ne mogu otvoriti trace datoteku");
    char line[256];
    uint64_t count=0;
    while(fgets(line,sizeof(line),f)){
        // preskoči prazne linije i komentare
        int allspace=1;
        for(char *p=line;*p;p++) if(!isspace((unsigned char)*p)){allspace=0;break;}
        if(allspace || line[0]=='#'||(line[0]=='/'&&line[1]=='/')) continue;

        uint64_t addr=parse_address(line);
        cache_access(c,addr);
        count++;
    }
    fclose(f);
    return count;
}

/* -----------------------------
   Built-in test (ako nema trace)
------------------------------*/
static void run_builtin(Cache *c){
    const uint64_t seq[]={ // mali skup test adresa
        0x0000,0x0004,0x0008,0x000C,0x0010,0x0014,0x0018,0x001C,
        0x0020,0x0024,0x0028,0x002C,0x0030,0x0034,0x0038,0x003C,
        0x0000,0x0004,0x0008,0x000C,
        0x1000,0x1004,0x1008,0x100C,
        0x0000,0x0800,0x1000,0x1800
    };
    for(size_t i=0;i<sizeof(seq)/sizeof(seq[0]);i++) cache_access(c,seq[i]);
}

/* -----------------------------
   Main
------------------------------*/
static void usage(const char *prog){
    printf("Uporaba: %s --size <cache_B> --block <block_B> --assoc <ways> [--trace <putanja>]\n",prog);
}

int main(int argc,char **argv){
    // Zadane vrijednosti
    uint32_t cache_size=16384;
    uint32_t block_size=16;
    uint32_t ways=1;
    const char *trace=NULL;

    // Parsiranje argumenata iz komandne linije
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--size") && i+1<argc) cache_size=(uint32_t)strtoul(argv[++i],NULL,0);
        else if(!strcmp(argv[i],"--block") && i+1<argc) block_size=(uint32_t)strtoul(argv[++i],NULL,0);
        else if(!strcmp(argv[i],"--assoc") && i+1<argc) ways=(uint32_t)strtoul(argv[++i],NULL,0);
        else if(!strcmp(argv[i],"--trace") && i+1<argc) trace=argv[++i];
        else { usage(argv[0]); return 1;}
    }

    // Kreiranje cache-a
    Cache c=cache_create(cache_size,block_size,ways);

    // Ako je zadan trace → simulacija s datotekom
    if(trace){
        uint64_t n=run_trace(&c,trace);
        if(n==0) fprintf(stderr,"Upozorenje: trace je prazan ili nečitljiv.\n");
    }else{
        // inače pokreni ugrađeni test
        printf("Nije zadan --trace, pokrećem built-in test...\n");
        run_builtin(&c);
    }

    // Ispis statistike
    print_stats(&c);

    // Oslobađanje memorije
    cache_free(&c);
    return 0;
}
