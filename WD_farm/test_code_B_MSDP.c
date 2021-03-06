#include "mex.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>


#define Q 16
#define page_size 18336
#define QLC   4
#define CW_per_page   2
#define num_sym_per_bit		8


int grey_code_inv[16] = {6,5,7,14,9,12,8,13,3,4,2,15,10,11,1,0};
int sym_top_1[num_sym_per_bit] = {0,1,2,3,4,10,11,15};
int sym_up_1[num_sym_per_bit] = {0,1,8,9,10,11,12,13};
int sym_mid_1[num_sym_per_bit] = {0,1,2,7,8,13,14,15};
int sym_low_1[num_sym_per_bit] = {0,4,5,11,12,13,14,15};

int n, m;
int rmax, cmax;
int *row_weight, *col_weight;
int **row_col;
double **p_sent_given_rec_T;

// Lower page data stored first
char data_read[8*page_size];
char data_written[8*page_size];

double atanh2(double x)
{
  return log((1.0 + x) / (1.0 - x));  // returns 2*atanh(x)
}
double logtanh2(double x)
{
  return log(tanh(fabs(x*0.5)));  // returns log tanh |x|
}

int HamDist(int *x, int *y, int len)
{
  int i, sum = 0;
  for (i = 0; i < len; i++) {
    if (*x++ != *y++) sum++;
  }
  return sum;
}

int bsc(int x[], int y[], double p, double q0[])
{
  int i, num = 0, modified = 0;
  int *err = malloc(sizeof(int) * n);
  memset(err, 0, sizeof(int) * n);
  modified = n * p + 0.5;
  p = modified / (double)n; // correct error probability
  //printf("m/n=%g, ", (double)m/n);
  //printf("BSC channel entropy(rate) = %g (bits)\n",
  //       (-p*log(p)-(1-p)*log(1-p)) / log(2.0));
  while (num < modified) {
    i = rand() % n;
    if (err[i] == 1) continue;
    err[i] = 1;
    num++;
  }
  for (i = 0; i < n; i++) {
    y[i] = x[i] ^ err[i];
  }
  free(err);

  for (i = 0; i < n; i++) {
    double d = (1 - 2 * y[i]) * log((1.0 - p) / p);
    q0[i] = d;
  }
  return modified;
}

void enc(int y[], int s[])
{
  int i, j;
  for (j = 0; j < m; j++) {
    register int k = 0;
    for (i = 0; i < row_weight[j]; i++)
      k ^= y[row_col[j][i]];

    s[j] = k;
  }
}

int **malloc2Dint(int a, int b) // allocates array[a][b]
{
  int i;
  int **pp = malloc(sizeof(int *) * a);
  int *p = malloc(sizeof(int) * a * b);
  if (pp == NULL || p == NULL) exit(-1);
  for (i = 0; i < a; i++) {
    pp[i] = p + b*i;
  }
  return pp;
}
int ***malloc2Dintp(int a, int b) // allocates array[a][b]
{
  int i;
  int ***pp = malloc(sizeof(int **) * a);
  int **p = malloc(sizeof(int*) * a * b);
  if (pp == NULL || p == NULL) exit(-1);
  for (i = 0; i < a; i++) {
    pp[i] = p + b*i;
  }
  return pp;
}

double **malloc2Ddouble(int a, int b) // allocates array[a][b]
{
  int i;
  double **pp = malloc(sizeof(double *) * a);
  double *p = malloc(sizeof(double) * a * b);
  if (pp == NULL || p == NULL) exit(-1);
  for (i = 0; i < a; i++) {
    pp[i] = p + b*i;
  }
  return pp;
}

double ***malloc2Ddoublep(int a, int b) // allocates array[a][b]
{
  int i;
  double ***pp = malloc(sizeof(double **) * a);
  double **p = malloc(sizeof(double*) * a * b);
  if (pp == NULL || p == NULL) exit(-1);
  for (i = 0; i < a; i++) {
    pp[i] = p + b*i;
  }
  return pp;
}


double **qin, ***qin_row;
double **LogTanhtin, ***LogTanhtin_row;
int **Sgntin, ***Sgntin_row;
int *tmp_bit;
int *tmp_s;

int dec(double q0[], int s[], int loop_max)
{
  int i, j, k, loop;

  memset(*qin, 0, n * cmax * sizeof(double));

  for (loop = 0; loop < loop_max; loop++) {
    for (i = 0; i < n; i++) {
      double sum = q0[i];
      for (j = 0; j < col_weight[i]; j++)
        sum += qin[i][j];
      for (j = 0; j < col_weight[i]; j++) {
        double qout = sum - qin[i][j];
        if (qout < 0) {
          *LogTanhtin_row[i][j] = logtanh2(-qout);
          *Sgntin_row[i][j] = 1;
        } else {
          *LogTanhtin_row[i][j] = logtanh2(qout);
          *Sgntin_row[i][j] = 0;
        }
        //printf("v_msg_%d_%d = %d",i,j,qout);
      }
    }

    for (j = 0; j < m; j++) {
      int sgnprod = s[j];
      double logprod = 0;
      for (k = 0; k < row_weight[j]; k++) {
        logprod += LogTanhtin[j][k];
        sgnprod ^= Sgntin[j][k];
      }

      for (k = 0; k < row_weight[j]; k++) {
        double tout = atanh2(exp(logprod - LogTanhtin[j][k]));
        if(sgnprod != Sgntin[j][k]){
          *qin_row[j][k] = -tout;
          //printf("c_msg_%d_%d = %d",j,k,-tout);
        } else{
          *qin_row[j][k] = tout;
          //printf("c_msg_%d_%d = %d",j,k,tout);
        }
      }
    }

    for (i = 0; i < n; i++) {
      double sum = q0[i];
      for (j = 0; j < col_weight[i]; j++) {
        sum += qin[i][j];
      }

      tmp_bit[i] = (sum < 0) ? 1 : 0;
    }
    //printf("%2d:HamDist(x)=%d\n ", loop+1, HamDist(x, tmp_bit, n));

    enc(tmp_bit, tmp_s);
    i = HamDist(s, tmp_s, m);
    //printf("HamDist(s,synd(x^))=%d\n", i);
    if (i == 0)           // nothing more can be done
      return 0;
  }
  return 1;
}

void initdec(char *s)
{
  int **row_N;
  int **col_row, **col_N;
  int i, j, *count;
  FILE *fp = fopen(s, "rt");
  if (fp == NULL) {
    fprintf(stderr, "cannot open %s\n", s);
    exit(-2);
  }
  
  fscanf(fp, "%d%d", &n, &m);
  fscanf(fp, "%d%d", &cmax, &rmax);
  col_weight = malloc(sizeof(int) * n);
  for (i = 0; i < n; i++) {
    fscanf(fp, "%d", &col_weight[i]);
  }
  row_weight = malloc(sizeof(int) * m);
  for (j = 0; j < m; j++)
    fscanf(fp, "%d", &row_weight[j]);

  {//skip n lines
    for (i = 0; i < n; i++) {
      for (j = 0; j < col_weight[i]; j++)
        fscanf(fp, "%*d");
    }
  }
  
  count = malloc(sizeof(int) * n);
  memset(count, 0, sizeof(int) * n);
  qin = malloc2Ddouble(n, cmax);
  qin_row = malloc2Ddoublep(m, rmax);
  LogTanhtin     = malloc2Ddouble(m, rmax);
  LogTanhtin_row = malloc2Ddoublep(n, cmax);
  Sgntin     = malloc2Dint(m, rmax);
  Sgntin_row = malloc2Dintp(n, cmax);
  tmp_bit = malloc(sizeof(int) * n);
  tmp_s = malloc(sizeof(int) * m);

  row_col = malloc2Dint(m, rmax);
  row_N   = malloc2Dint(m, rmax);
  col_row = malloc2Dint(n, cmax);
  col_N   = malloc2Dint(n, cmax);
  for (j = 0; j < m; j++) {
    for (i = 0; i < row_weight[j]; i++) {
      int v;
      fscanf(fp, "%d", &v);
      v--;
      //if(j == 0){
      //  printf("%d\n", v);
      //}
      row_col[j][i] = v;	// col address
      row_N[j][i] = count[v];	// vertical count of non-zero coef
      col_row[v][count[v]] = j;	// row address
      col_N[v][count[v]] = i;	// horizontal count of non-zero coef
      count[v]++;
      qin_row[j][i] = &qin[row_col[j][i]][row_N[j][i]];
    }
    // following block added on 02/05/2008 according to Mr. David Elkouss' comment
    for ( ; i < rmax; i++) {
      fscanf(fp, "%*d"); // skip the 0s (fillers)
    }
  }
  fclose(fp);

  for (i = 0; i < n; i++) {
    for (j = 0; j < col_weight[i]; j++) {
      LogTanhtin_row[i][j] = &LogTanhtin[col_row[i][j]][col_N[i][j]];
      Sgntin_row[i][j] =     &Sgntin    [col_row[i][j]][col_N[i][j]];
    }
  }

  free(count);
  free(*row_N);
  free( row_N);
  free(*col_row);
  free( col_row);
  free(*col_N);
  free( col_N);
}

// p_rec_given_sent[i][j] = P(i rec | j sent)
// p_sent_given_rec_T[i][j] = P(j sent | i rec)
void make_p_sent_given_rec_T(double* p_rec_given_sent, int num_reads){
  int i, j, row_dim;
  double P_x = 1/(1.0*Q);
  double P_y;

  if(num_reads == 1){
    row_dim = Q;
  } else if(num_reads == 3){
    row_dim = Q*num_reads-2;
  }

  for(i = 0; i < row_dim; i++){
    P_y = 0;
    for(j = 0; j < Q; j++){
      P_y += p_rec_given_sent[i*Q+j]*P_x;
    }
    if(P_y){
      for(j = 0; j < Q; j++){
        p_sent_given_rec_T[i][j] = P_x*p_rec_given_sent[i*Q+j]/P_y;
      }
    } else{
      for(j = 0; j < Q; j++){
        p_sent_given_rec_T[i][j] = 0;
      }
    }
  }
}

void get_bits_in_symbol(char symbol, int x[], int symbol_ind){
  int lp_bit, mp_bit, up_bit, tp_bit;

  lp_bit = symbol & 1;
  mp_bit = (symbol >> 1) & 1;
  up_bit = (symbol >> 2) & 1;
  tp_bit = (symbol >> 3) & 1;

  x[4*symbol_ind] = lp_bit;
  x[4*symbol_ind+1] = mp_bit;
  x[4*symbol_ind+2] = up_bit;
  x[4*symbol_ind+3] = tp_bit;
}

void assign_llr_one_sym(int sym, int sym_ind, double q0[]){
  double Pr_1, llr;
  int j;

  // On lower page
    Pr_1 = 0;
    for(j = 0; j < num_sym_per_bit; j++){
      Pr_1 += p_sent_given_rec_T[sym][sym_low_1[j]];
    }
    if(Pr_1 > 0.99999){
      llr = -9.21;
    } else if(Pr_1 == 0){
      llr = 9.21;
    } else{
      llr = log((1.0 - Pr_1) / Pr_1);
      /*if(llr > 30 || llr < -30){
        printf("%.4f\n", Pr_1);
        printf("%.4f\n", (1.0 - Pr_1) / Pr_1);
        printf("%.4f\n", llr);
      }*/
    }
    q0[4*sym_ind] = llr;
    // On middle page
    Pr_1 = 0;
    for(j = 0; j < num_sym_per_bit; j++){
      Pr_1 += p_sent_given_rec_T[sym][sym_mid_1[j]];
    }
    if(Pr_1 > 0.99999){
      llr = -9.21;
    } else if(Pr_1 == 0){
      llr = 9.21;
    } else{
      llr = log((1.0 - Pr_1) / Pr_1);
      /*if(llr > 30 || llr < -30){
        printf("%.4f\n", Pr_1);
        printf("%.4f\n", (1.0 - Pr_1) / Pr_1);
        printf("%.4f\n", llr);
      }*/
    }
    q0[4*sym_ind+1] = llr;
    // On upper page
    Pr_1 = 0;
    for(j = 0; j < num_sym_per_bit; j++){
      Pr_1 += p_sent_given_rec_T[sym][sym_up_1[j]];
    }
    if(Pr_1 > 0.99999){
      llr = -9.21;
    } else if(Pr_1 == 0){
      llr = 9.21;
    } else{
      llr = log((1.0 - Pr_1) / Pr_1);
      /*if(llr > 30 || llr < -30){
        printf("%.4f\n", Pr_1);
        printf("%.4f\n", (1.0 - Pr_1) / Pr_1);
        printf("%.4f\n", llr);
      }*/
    }
    q0[4*sym_ind+2] = llr;
    // On top page
    Pr_1 = 0;
    for(j = 0; j < num_sym_per_bit; j++){
      Pr_1 += p_sent_given_rec_T[sym][sym_top_1[j]];
    }
    if(Pr_1 > 0.99999){
      llr = -9.21;
    } else if(Pr_1 == 0){
      llr = 9.21;
    } else{
      llr = log((1.0 - Pr_1) / Pr_1);
      /*if(llr > 30 || llr < -30){
        printf("%.4f\n", Pr_1);
        printf("%.4f\n", (1.0 - Pr_1) / Pr_1);
        printf("%.4f\n", llr);
      }*/
    }
    q0[4*sym_ind+3] = llr;
}

// Works only if n is multiple of 4 (a single QLC cell is not shared
// between 2 CWs)
void assign_llr(int y[], double q0[]){
  int i, j;
  char symbol;
  double Pr_1, llr;

  for(i = 0; i < n/4; i++){
    symbol = y[i];

    assign_llr_one_sym(symbol, i, q0);
  }
}

void channel(int x[], double* p_rec_given_sent, double q0[], int num_reads){
  int i, symbol, rand_select, rec_ind, row_dim;
  double temp;

  if(num_reads == 1){
    row_dim = Q;
  } else if(num_reads == 3){
    row_dim = Q*num_reads-2;
  }

  for(i = 0; i < n/4; i++){
    symbol = grey_code_inv[(x[4*i+3] << 3) + (x[4*i+2] << 2) + (x[4*i+1] << 1) + (x[4*i])];
    //fprintf(debug_file, "%d ", symbol);
    
    temp = 0;
    rec_ind = 0;
    rand_select = rand()%10001;
    
    if(!rand_select){
      while(!p_rec_given_sent[(rec_ind++)*Q+symbol]);
    }

    while(temp < rand_select && rec_ind != row_dim){
      temp += 10000*p_rec_given_sent[(rec_ind++)*Q+symbol];
    }
    
    symbol = --rec_ind;
    //fprintf(debug_file, "%d ", symbol);
    
    assign_llr_one_sym(symbol, i, q0);
    //break;
  }
  //fprintf(debug_file, "\n");
}

void test_code_B_MSDP(int iteration, int num_trials, int num_reads, int decode_mode, double* p_rec_given_sent, double *errors){
  srand(time(NULL));
  int i, j, k, dec_result, *iterations, *s, *x, *y, row_dim;
  int CW_per_page_fetched;
  char symbol;
  double *q0;

  if(num_reads == 1){
    row_dim = Q;
  } else if(num_reads == 3){
    row_dim = Q*num_reads-2;
  }

  //printf("row_dim = %d\n", row_dim);

  // Normalize p_rec_given_sent
  double norm;
  for(i = 0; i < Q; i++){
    norm = 0;
    for(j = 0; j < row_dim; j++){
      norm += p_rec_given_sent[j*Q+i];
    }
    for(j = 0; j < row_dim; j++){
      p_rec_given_sent[j*Q+i] /= norm;
    }
  }
  
  initdec("peg_16000_3_0.9.txt");
  p_sent_given_rec_T = malloc2Ddouble(row_dim, Q);
  q0= malloc(sizeof(double) * n);
  s = malloc(sizeof(int) * m);  // syndrome
  x = malloc(sizeof(int) * n);  // source
  y = malloc(sizeof(int) * n);  // side information
  
  make_p_sent_given_rec_T(p_rec_given_sent, num_reads);

  /*printf("p_rec_given_sent = \n");
  for(i = 0; i < row_dim; i++){
    for(j = 0; j < Q; j++){
      printf("%.2f ", p_rec_given_sent[i*Q+j]);
    }
    printf("\n");
  }
  printf("p_sent_given_rec_T = \n");
  for(i = 0; i < row_dim; i++){
    for(j = 0; j < Q; j++){
      printf("%.4f ", p_sent_given_rec_T[i][j]);
    }
    printf("\n");
  }*/

  errors[0] = 0;
  errors[1] = 0;

  // Start Timer
  clock_t start = clock(), diff;

  if(decode_mode){
    FILE *read_data = fopen("snowbird_sym.bin", "rb");
    FILE *written_data = fopen("snowbird_sym.bin", "rb");

    while(num_trials){
      fread(data_read, 1, 8*page_size, read_data);
      fread(data_written, 1, 8*page_size, written_data);
      CW_per_page_fetched = 0;

      while(CW_per_page_fetched != CW_per_page){
        for(i = 0; i < n/4; i++){
            symbol = data_written[CW_per_page_fetched*n/4+i];
            get_bits_in_symbol(symbol, x, i);
            symbol = data_read[CW_per_page_fetched*n/4+i];
            y[i] = symbol;
        }

        enc(x, s);
        assign_llr(y, q0);
    
        dec_result = dec(q0, s, iteration);
    
        if(dec_result){
            errors[0]++;
        } else {
            if(HamDist(tmp_bit, x, n) != 0) errors[1]++;
        }
        CW_per_page_fetched++;
        num_trials--;
      }
    }

    fclose(read_data);
    fclose(written_data);
  } else{
    FILE *debug_f = fopen("debug.txt", "w");
    for (j = 1; j <= num_trials; j++){
      // Generate random data
      for (i = 0; i < n; i++){
        x[i] = rand() % 2;
      }

      enc(x, s);
      /*for(i = 0; i < n; i++){
        fprintf(debug_f, "%d ", x[i]);
      }
      fprintf(debug_f, "\n");*/
      channel(x, p_rec_given_sent, q0, num_reads);
      /*for(i = 0; i < n; i++){
        fprintf(debug_f, "%.3f ", q0[i]);
      }
      fprintf(debug_f, "\n");*/

      dec_result = dec(q0, s, iteration);

      if(dec_result){
          errors[0]++;
      } else {
          if(HamDist(tmp_bit, x, n) != 0) errors[1]++;
      }
    }
    fclose(debug_f);
  }
  
  // End Timer
  diff = clock() - start;
  int msec = diff * 1000 / CLOCKS_PER_SEC;
  //printf("Time taken %d seconds %d milliseconds\n", msec/1000, msec%1000);

  /*printf("Undetected Errors = %f\n", errors[1]);
  printf("Errors = %f\n", errors[0]);*/
}


/* The gateway function */
void mexFunction( int nlhs, mxArray *plhs[],
                  int nrhs, const mxArray *prhs[])
{
    double *conf_mat;              // input scalar
    int num_trials;              // input scalar
    int max_iter;              // input scalar
    int num_reads;               // input scalar
    int decode_mode;          // input scalar
    
    /* check for proper number of arguments */
    if(nrhs!=5) {
        mexErrMsgIdAndTxt("MyToolbox:arrayProduct:nrhs","Five inputs required.");
    }
    if(nlhs!=1) {
        mexErrMsgIdAndTxt("MyToolbox:arrayProduct:nlhs","Two output required.");
    }
    
    /* get the values of the inputs  */
    max_iter = mxGetScalar(prhs[0]);
    num_trials = mxGetScalar(prhs[1]);
    num_reads = mxGetScalar(prhs[2]);
    decode_mode = mxGetScalar(prhs[3]);
    conf_mat = mxGetPr(prhs[4]);
    
    /* create the output matrix */
    plhs[0] = mxCreateNumericMatrix(1,2,mxDOUBLE_CLASS,mxREAL);
    
    /* get a pointer to the real data in the output matrix */
    double *outMatrix = mxGetPr(plhs[0]);

    /* call the computational routine */
    test_code_B_MSDP(max_iter, num_trials, num_reads, decode_mode, conf_mat, outMatrix);
}
