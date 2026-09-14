/*
 * framelist.h - Lista doblemente enlazada *intrusiva* de marcos.
 *
 * Utilidad interna compartida por FIFO y LRU: los nodos no se asignan con
 * malloc, viven en arreglos indexados por numero de marco, asi que insertar,
 * mover al final y sacar del frente son O(1) exactos (nada de borrado perezoso
 * ni entradas obsoletas cuando un marco se libera y se vuelve a usar).
 *
 * Con esta lista, FIFO y LRU se diferencian en una sola linea: que hace
 * on_access. FIFO no hace nada; LRU manda el marco al final.
 */
#ifndef FRAMELIST_H
#define FRAMELIST_H

typedef struct FrameList FrameList;

FrameList *fl_create(int nframes);
void       fl_destroy(FrameList *l);

void       fl_push_back(FrameList *l, int frame); /* al final (mas reciente)  */
void       fl_move_back(FrameList *l, int frame); /* refresca su posicion     */
void       fl_remove(FrameList *l, int frame);    /* lo saca si esta          */
int        fl_pop_front(FrameList *l);            /* frente, o -1 si vacia    */

#endif /* FRAMELIST_H */
