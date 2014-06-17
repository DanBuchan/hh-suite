/*
 * BacktraceMAC.C
 *
 *  Created on: May 1, 2014
 *      Author: stefan
 */

#include "hhposteriordecoder.h"
#include "hhviterbi.h"
#include <stddef.h>

void PosteriorDecoder::backtraceMAC(HMM & q, HMM & t, PosteriorMatrix & p_mm, ViterbiMatrix & backtrace_matrix, const int elem, Hit & hit, float corr) {

	// Trace back trough the matrix b[i][j] until STOP state is found

    LogLevel actual_level = Log::reporting_level();
	int step;      // counts steps in path through 5-layered dynamic programming matrix
	int i,j;       // query and template match state indices

	initializeBacktrace(t,hit);

	// Make sure that backtracing stops when t:M1 or q:M1 is reached (Start state), e.g. sMM[i][1], or sIM[i][1] (M:MM, B:IM)
	for (i = 0; i <= q.L; ++i) backtrace_matrix.setMatMat(i, 1, elem, ViterbiMatrix::STOP);	// b[i][1] = STOP;
	for (j = 1; j <= t.L; ++j) backtrace_matrix.setMatMat(1, j, elem, ViterbiMatrix::STOP);	// b[1][j] = STOP;

	// Back-tracing loop
	// In contrast to the Viterbi-Backtracing, STOP signifies the first Match-Match state, NOT the state before the first MM state
	hit.matched_cols = 1; // for each MACTH (or STOP) state matched_col is incremented by 1
	hit.state = ViterbiMatrix::MM;       // lowest state with maximum score must be match-match state
	step = 0;         // steps through the matrix correspond to alignment columns (from 1 to nsteps)
	i = hit.i2; j = hit.j2;     // last aligned pair is (i2,j2)
	if (backtrace_matrix.getMatMat(i, j, elem) != ViterbiMatrix::MM) {		// b[i][j] != MM
		if (Log::reporting_level() > LogLevel::DEBUG)
		  fprintf(stderr,"Error: backtrace does not start in match-match state, but in state %i, (i,j)=(%i,%i)\n",backtrace_matrix.getMatMat(i, j, elem),i,j);

		step = 1;
		hit.i[step] = i;
		hit.j[step] = j;
		hit.alt_i->push_back(i);
		hit.alt_j->push_back(j);
		hit.state = ViterbiMatrix::STOP;
	} else {
		while (hit.state != ViterbiMatrix::STOP) {
			step++;
			hit.states[step] = hit.state = backtrace_matrix.getMatMat(i, j, elem); // b[i][j];
			hit.i[step] = i;
			hit.j[step] = j;
			hit.alt_i->push_back(i);
			hit.alt_j->push_back(j);
			// Exclude cells in direct neighbourhood from all further alignments
			for (int ii = imax(i-2,1); ii <= imin(i+2, q.L); ++ii)
//				hit.cell_off[ii][j] = 1;
				backtrace_matrix.setCellOff(ii, j, elem, true);
			for (int jj = imax(j-2,1); jj <= imin(j+2, t.L); ++jj)
				backtrace_matrix.setCellOff(i, jj, elem, true);

			if (hit.state == ViterbiMatrix::MM) hit.matched_cols++;

			switch (hit.state) {
				case ViterbiMatrix::MM: i--; j--; break;
				case ViterbiMatrix::IM: j--; break;
				case ViterbiMatrix::MI: i--; break;
				case ViterbiMatrix::STOP: break;
				default:
					fprintf(stderr,"Error: unallowed state value %i occurred during backtracing at step %i, (i,j)=(%i,%i)\n", hit.state, step, i, j);
					hit.state = 0;
					actual_level = LogLevel::DEBUG1;
					break;
			} //end switch (state)
		} //end while (state)
	}
	hit.i1 = hit.i[step];
	hit.j1 = hit.j[step];
	hit.states[step] = ViterbiMatrix::MM;  // first state (STOP state) is set to MM state
	hit.nsteps = step;

	// Allocate new space for alignment scores
	hit.S    = new( float[hit.nsteps+1]);
	hit.S_ss = new( float[hit.nsteps+1]);
	hit.P_posterior = new( float[hit.nsteps+1]);

	if (!hit.P_posterior)
		MemoryError("space for HMM-HMM alignments", __FILE__, __LINE__, __func__);

	// Add contribution from secondary structure score, record score along alignment,
	// and record template consensus sequence in master-slave-alignment to query sequence
	hit.score_ss = 0.0f;
	hit.sum_of_probs = 0.0;       // number of identical residues in query and template sequence
	int ssm = hit.ssm1 + hit.ssm2;
	//   printf("Hit=%s\n",name); /////////////////////////////////////////////////////////////
	for (step = 1; step <= hit.nsteps; step++) {
		switch(hit.states[step]) {
		case ViterbiMatrix::MM:
			i = hit.i[step];
			j = hit.j[step];

			hit.S[step] = Viterbi::Score(q.p[i], t.p[j]);
			hit.S_ss[step] = Viterbi::ScoreSS(&q, &t, i, j, ssm);
			hit.score_ss += hit.S_ss[step];
//			hit.P_posterior[step] = powf(2, p_mm.getSingleValue(hit.i[step], hit.j[step], elem));
			hit.P_posterior[step] = fpow2(p_mm.getSingleValue(hit.i[step], hit.j[step], elem));
			// Add probability to sum of probs if no dssp states given or dssp states exist and state is resolved in 3D structure
			if (t.nss_dssp<0 || t.ss_dssp[j]>0)
				hit.sum_of_probs += hit.P_posterior[step];
			// 	  printf("j=%-3i  dssp=%1i  P=%4.2f  sum=%6.2f\n",j,t.ss_dssp[j],P_posterior[step],sum_of_probs); //////////////////////////
			break;
		case ViterbiMatrix::MI: //if gap in template
		case ViterbiMatrix::DG:
		default: //if gap in T or Q
			hit.S[step] = hit.S_ss[step] = hit.P_posterior[step] = 0.0;
			break;
		}
	}
	//   printf("\n"); /////////////////////////////////////////////////////////////
	if (hit.ssm2 >= 1)
		hit.score -= hit.score_ss;    // subtract SS score added during alignment!!!!

	// Add contribution from correlation of neighboring columns to score
	float Scorr = 0;
	if (hit.nsteps) {
				for (step = 1; step <= hit.nsteps-1; step++) Scorr += hit.S[step] * hit.S[step+1];
				for (step = 1; step <= hit.nsteps-2; step++) Scorr += hit.S[step] * hit.S[step+2];
				for (step = 1; step <= hit.nsteps-3; step++) Scorr += hit.S[step] * hit.S[step+3];
				for (step = 1; step <= hit.nsteps-4; step++) Scorr += hit.S[step] * hit.S[step+4];
				hit.score += corr * Scorr;
	}

	// Set score, P-value etc.
	hit.score_sort = hit.score_aass = -hit.score;
	hit.logPval = 0; hit.Pval = 1;
	if (t.mu) {
		hit.logPvalt = logPvalue(hit.score, t.lamda, t.mu);
		hit.Pvalt = Pvalue(hit.score,t.lamda,t.mu);
	} else {
		hit.logPvalt = 0;
		hit.Pvalt = 1;
	}
	//   printf("%-10.10s lamda=%-9f  score=%-9f  logPval=%-9g\n",name,t.lamda,score,logPvalt);

	//DEBUG: Print out MAC alignment path
	if (actual_level >= LogLevel::DEBUG1) {
				float sum_post = 0.0;
				printf("NAME=%7.7s score=%7.3f  score_ss=%7.3f\n", hit.name, hit.score, hit.score_ss);
				printf("step  Q T    i    j  state   score    T Q cf ss-score   P_post Sum_post\n");
				for (step = hit.nsteps; step >= 1; step--) {
						switch(hit.states[step]) {
								case ViterbiMatrix::MM:
										sum_post += hit.P_posterior[step];
										printf("%4i  %1c %1c ",step,q.seq[q.nfirst][hit.i[step]], hit.seq[hit.nfirst][hit.j[step]]);
										break;
								case ViterbiMatrix::IM:
										printf("%4i  - %1c ",step, hit.seq[hit.nfirst][hit.j[step]]);
										break;
								case ViterbiMatrix::MI:
										printf("%4i  %1c - ",step,q.seq[q.nfirst][hit.i[step]]);
										break;
				}
						printf("%4i %4i     %2i %7.1f    ", hit.i[step], hit.j[step],(int)hit.states[step], hit.S[step]);
						printf("%c %c  %1i  %7.1f  ", i2ss(t.ss_dssp[hit.j[step]]),i2ss(q.ss_pred[hit.i[step]]),q.ss_conf[hit.i[step]]-1, hit.S_ss[step]);
						printf("%7.5f  %7.2f\n", hit.P_posterior[step],sum_post);
				}
	}

	return;

}

/////////////////////////////////////////////////////////////////////////////////////
// Allocate memory for data of new alignment (sequence names, alignment, scores,...)
/////////////////////////////////////////////////////////////////////////////////////
void PosteriorDecoder::initializeBacktrace(HMM & t, Hit & hit) {
	// Allocate new space
    if(hit.i) {
      delete[] hit.i;
    }
	hit.i = new (int[hit.i2 + hit.j2 + 2]);

	if(hit.j) {
	  delete[] hit.j;
	}
	hit.j = new (int[hit.i2 + hit.j2 + 2]);

	if(hit.states) {
	  delete[] hit.states;
	}
	hit.states = new (char[hit.i2 + hit.j2 + 2]);

	if(hit.S) {
	  delete[] hit.S;
	  hit.S = NULL;
	}

	if(hit.S_ss) {
	  delete[] hit.S_ss;
	  hit.S_ss = NULL;
	}

	if(hit.P_posterior) {
	  delete[] hit.P_posterior;
	  hit.P_posterior = NULL;
	}
}


