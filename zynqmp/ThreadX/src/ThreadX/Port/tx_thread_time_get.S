	.text
    .align 3
    .global _tx_thread_time_get
    .type  _tx_thread_time_get, @function
_tx_thread_time_get:
    MRS     x0, CNTPCT_EL0
    UBFX    x0, x0, #0, #32
    RET
