#include <stdlib.h>
#include "rnn_data.c"

rnn_weight *munge(const rnn_weight *a, int M, int N) {
  rnn_weight *b = calloc(N, M), *bp = b;
  for(int i=0; i<N; i++, bp+=M)
    for(int j=0; j < M; j++)
      bp[j] = a[j*N+i];
  return b;
}

void dump(char *name, char *type, const rnn_weight *a, int M, int N, int P) {
  fprintf(stdout, "static const rnn_weight %s_%s[%d] = {\n", name, type, M*N*P);
  fprintf(stdout, " %4d", a[0]);
  for(int j = 1; j < M*N*P; j++) {
    fprintf(stdout, ",");
    if( j && (j % N) == 0) fprintf(stdout, "\n");
    fprintf(stdout, " %4d", a[j]);
  }
  fprintf(stdout, "};\n\n");
}

#define DB(x,a,s) dump(#x,"bias", a, 1, x.nb_neurons, s); 
#define DW(x,a,s) dump(#x,"weights", a, x.nb_inputs, x.nb_neurons, s);
#define DR(x,a,s) dump(#x,"recurrent_weights", a, x.nb_neurons, x.nb_neurons, s);

#define MW(x,w,s) do {\
  rnn_weight *tmp = munge(x.input_weights, x.nb_inputs, x.nb_neurons * s);\
  DW(x, tmp, s);\
  free(tmp);\
} while(0);

#define MR(x,w,s) do {\
  rnn_weight *tmp = munge(x.recurrent_weights, x.nb_neurons, x.nb_neurons * s);\
  DR(x, tmp, s);\
  free(tmp);\
} while(0);

int main(int argc, char *argv) {
  MW(input_dense, input_dense_weights, 1);
  DB(input_dense, input_dense_bias, 1); 

  MW(vad_gru, vad_gru_weights, 3);
  MR(vad_gru, vad_gru_recurrent_weights, 3);
  DB(vad_gru, vad_gru_bias, 3); 

  MW(noise_gru, noise_gru_weights, 3);
  MR(noise_gru, noise_gru_recurrent_weights, 3);
  DB(noise_gru, noise_gru_bias, 3); 

  MW(denoise_gru, denoise_gru_weights, 3);
  MR(denoise_gru, denoise_gru_recurrent_weights, 3); 
  DB(denoise_gru, denoise_gru_bias, 3); 

  MW(denoise_output, denoise_output_weights, 1);
  DB(denoise_output, denoise_output_bias, 1); 
 
  MW(vad_output, vad_output_weights, 1);
  DB(vad_output, vad_output_bias,1); 
}
