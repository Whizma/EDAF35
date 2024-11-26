fac(2) uses 9 different pages and with 8 ram pages the minimum number of pagefaults is 10, since one of the pages is only used once. Look at trace[45] print. 

Optimal is not practical since we need to know the future in order to know the optimal. There also might be other external factors such as IO. 

FIFO:

Pages are replaced by First-In-First-out policy, meaning that the oldest page is replaced first. The 


Second chance:

Pages contain a reference bit to which tells the replacement agorithm to keep it since it has been used recetly and set the bit to 0. The replacement algorithm loops the pages until it finds a page with