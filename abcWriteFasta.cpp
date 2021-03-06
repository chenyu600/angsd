/*
thorfinn 31oct 2014
refactored. Should be simpler now
*/



#include <cmath>
#include <cstdlib>
#include "bgzf.h"
#include <assert.h>

#include "analysisFunction.h"
#include "shared.h"
#include "kstring.h"//<-used for buffered output
#include "abc.h"
#include "abcWriteFasta.h"


void abcWriteFasta::printArg(FILE *argFile){
  fprintf(argFile,"--------------\n%s:\n",__FILE__);
  fprintf(argFile,"\t-doFasta\t%d\n",doFasta);
  fprintf(argFile,"\t1: use a random base\n");
  fprintf(argFile,"\t2: use the most common base (needs -doCounts 1)\n");
  fprintf(argFile,"\t3: use the base with highest ebd (under development) \n");
  fprintf(argFile,"\t-basesPerLine\t%d\t(Number of bases perline in output file)\n",NbasesPerLine);
  fprintf(argFile,"\t-explode\t%d\t(Should we include chrs with no data?)",explode);
  fprintf(argFile,"\n");
}

void abcWriteFasta::getOptions(argStruct *arguments){

  //from command line
  doFasta=angsd::getArg("-doFasta",doFasta,arguments);
  doCount=angsd::getArg("-doCounts",doCount,arguments);
  explode=angsd::getArg("-explode",explode,arguments);
  NbasesPerLine = angsd::getArg("-basesPerLine",NbasesPerLine,arguments);
  
  if(doFasta){
    if(arguments->inputtype!=INPUT_BAM&&arguments->inputtype!=INPUT_PILEUP){
      fprintf(stderr,"Error: bam or soap input needed for -doFasta \n");
      exit(0);
    }
    if(arguments->nInd!=1){
      fprintf(stderr,"Error: -doFasta only uses a single individual\n");
      exit(0);
    }
    if(doFasta==2 & doCount==0){
      fprintf(stderr,"Error: -doFasta 2 needs allele counts (use -doCounts 1)\n");
      exit(0);
    }
  }


}
void writeChr(kstring_t *bufstr,size_t len,char *nam,char*d,int nbpl){
  fprintf(stderr,"\t[%s] writing chr:%s\n",__FUNCTION__,nam);
  ksprintf(bufstr,">%s",nam);
  for(size_t i=0;i<len;i++){
    if(i % nbpl == 0)
      kputc('\n',bufstr);
    kputc(d!=NULL?d[i]:'N',bufstr);
  }
  kputc('\n',bufstr);

}


abcWriteFasta::abcWriteFasta(const char *outfiles,argStruct *arguments,int inputtype){
  explode = 0;
  myFasta = NULL;
  doFasta=0;
  doCount=0;
  currentChr=-1;
  NbasesPerLine=50;
  hasData =0;
  if(arguments->argc==2){
    if(!strcasecmp(arguments->argv[1],"-doFasta")){
      printArg(stdout);
      exit(0);
    }else
      return;
  }
  
  getOptions(arguments);
  printArg(arguments->argumentFile);

  if(doFasta==0){
    shouldRun[index] = 0;
    return;

  }
  for(int i=0;i<256;i++)
    lphred[i] =log(1.0-pow(i,-1.0*i/10.0));
  //make output files
  const char* postfix;
  postfix=".fa.gz";
  outfileZ = Z_NULL;
  outfileZ = aio::openFileBG(outfiles,postfix,GZOPT);
  bufstr.s=NULL;
  bufstr.m=0;
  bufstr.l=0;

}


abcWriteFasta::~abcWriteFasta(){
  if(doFasta==0)
    return;
  changeChr(-1);
  if(outfileZ!=Z_NULL) bgzf_close(outfileZ); 
  if(bufstr.s!=NULL)
    free(bufstr.s);
}

void abcWriteFasta::changeChr(int refId) {
  if(doFasta==0)
    return;
  if(myFasta!=NULL){//proper case we have data
    if(explode||hasData){
      writeChr(&bufstr,header->l_ref[currentChr],header->name[currentChr],myFasta,NbasesPerLine);
    bgzf_write(outfileZ,bufstr.s,bufstr.l);bufstr.l=0;
    }
  }
  
  //ANDERS FILL IN MISSING CHRS IF YOU WANT HERE
  //ANDERS IS APPRANTLY LAZY SO NOW I'VE DONE IT FOR HIM

  if(refId!=-1){//-1 = destructor
    for(int i=currentChr+1;explode&&i<refId;i++){
      writeChr(&bufstr,header->l_ref[i],header->name[i],NULL,NbasesPerLine);
      bgzf_write(outfileZ,bufstr.s,bufstr.l);bufstr.l=0;
    }
    currentChr=refId;
    free(myFasta);
    myFasta=(char*)malloc(header->l_ref[currentChr]);
    memset(myFasta,'N',header->l_ref[currentChr]);
  }else{
    free(myFasta);
    for(int i=currentChr+1;explode&&i<header->n_ref;i++){
      writeChr(&bufstr,header->l_ref[i],header->name[i],NULL,NbasesPerLine);
      bgzf_write(outfileZ,bufstr.s,bufstr.l);bufstr.l=0;
    }
  }
  bgzf_write(outfileZ,bufstr.s,bufstr.l);bufstr.l=0;
}




void abcWriteFasta::run(funkyPars *pars){

  if(doFasta==0)
    return;
  hasData=1;
  if(doFasta==1){//random number read
    for(int s=0;s<pars->numSites&&pars->posi[s]<header->l_ref[pars->refId];s++){
      if(pars->keepSites[s]==0)
	continue;
      if(pars->chk->nd[s][0].l==0)
	continue;
      int j = std::rand() % pars->chk->nd[s][0].l;
      myFasta[pars->posi[s]] = pars->chk->nd[s][0].seq[j];
      
    }
  }else if(doFasta==2) {//most common
    for(int s=0;s<pars->numSites&&pars->posi[s]<header->l_ref[pars->refId];s++){
      if(pars->keepSites[s]==0)
	continue;

      int max=0;
      for( int b = 0; b < 4; b++ ){
	if( pars->counts[s][b] >= max){
	  max=pars->counts[s][b];
	}
      }
      if(max==0)
	continue;

      int nmax=0;
      int w=-1;
      for( int b = 0; b < 4; b++ ){
	if( pars->counts[s][b] == max ){
	  w=b;
	  nmax++;
	}
      }

      if(nmax==1)
	myFasta[pars->posi[s]] = intToRef[w];
      else{
	int cumsum[4] = {-1,-1,-1,-1};
	int i=0;
	for( int b = 0; b < 4; b++ ){
	  if( pars->counts[s][b] == max ){
	    cumsum[b] = i;
	    i++;
	  }
 	}

	int j = std::rand() % i;

	for( int b = 0; b < 4; b++ ){
	  if( cumsum[b] ==j ){
	    myFasta[pars->posi[s]] = intToRef[b];
	    break;
	  }
	}
      }
    }
  }else if(doFasta==3){
    for(int i=0;i<pars->nInd;i++){
      //      fprintf(stderr,"numSites: %d\n",pars->numSites);
      for(int s=0;s<pars->numSites&&pars->posi[s]<header->l_ref[pars->refId];s++){
	tNode &tn = pars->chk->nd[s][i];
	double ebds[]= {0.0,0.0,0.0,0.0};
	for(int b=0;b<tn.l;b++){
	  int bof = refToInt[tn.seq[b]];
	  if(bof==4)
	    continue;
	  //	  fprintf(stderr,"pos:%d b:%d  bas:%c mapQ:%d qxcore:%c bof:%d qs:%d mapQ:%d %f %f \n",pars->posi[s],b,tn.seq[b],tn.mapQ[b],33+tn.qs[b],bof,tn.qs[b],tn.mapQ[b],lphred[tn.qs[b]],lphred[tn.mapQ[b]]);
	  ebds[bof] += exp(lphred[tn.qs[b]]+lphred[tn.mapQ[b]]);

	}
	for(int b=0;0&&b<4;b++)
	  fprintf(stderr,"b:%d %f\n",b,ebds[b]);
	int wh = angsd::whichMax(ebds,4);
	if(wh==-1) wh=4;//catch no information
	myFasta[pars->posi[s]] = intToRef[wh];
      }
    }
  }
}

