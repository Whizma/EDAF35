## Optimal page replacement

fac(2) uses 9 different pages and with 8 ram pages the minimum number of pagefaults is 10, since one of the pages is only used once. Look at the trace[45] print. 

Optimal page replacement is not practical since we need to know the future in order to know the optimal. Other external dynamic factors such as IO could also hinder the future simulaton. Seing the future is difficult, so we try to simulate optimal page replacement using second chance or LRU replacement. 

## FIFO and second chance algorithms

#### FIFO:

Pages are replaced by First-In-First-out policy, meaning that the oldest page is replaced first. The implementation is rather easy, but increasing the number of frames using the FIFO replacement policy could lead to more page faults, known as Beladys anomaly. 

#### Second chance:

Pages contain a reference bit which tells the replacement algorithm to keep the page in memory, since it has been used recently and set the bit to 0. When the page is accessed the bit is set to 1. The replacement algorithm loops the pages until it finds a page with the reference bit 0.

## Comparison

Comparing the two algorithms above, the FIFO implementation is simple and gives good enough performance, but might suffer if it evicts frequently accessed pages. The second chance algorithm works similarly, but wont evict recently accessed pages. This introduces some overhead, and in the worst case where all pages have their refereced bit set to 1, all pages would be looped through. 