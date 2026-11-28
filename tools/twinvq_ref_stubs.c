/* Test-fixture tool: stubs of the parts of the reference software that the TwinVQ driver (twinvq_ref_driver.c) does not use (error resilience flags, access unit bookkeeping, scalable layers). */
int GetEPFlag(void*a){return 0;} int GetReorderSpecFlag(void*a){return 0;} int GetConsecutiveReorderSpecFlag(void*a){return 0;} int GetRvlcScfFlag(void*a){return 0;}
int GetVcb11Flag(void*a){return 0;} int GetLenOfScfDataFlag(void*a){return 0;} int GetLenOfLongestCwFlag(void*a){return 0;} int GetScfBitFlag(void*a){return 0;}
int GetReorderSpecPreSortFlag(void*a){return 0;} int GetScfConcealmentFlag(void*a){return 0;}
void removeAU(void*a,int b,void*c,int d){}
void mat_scale_tf_requantize_spectrum(void){} void mat_scale_set_shift_para2(void){}
