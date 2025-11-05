#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

typedef struct {
    int BankAccount;  // shared account balance
    int Turn;         // 0 = Parent's turn, 1 = Child's turn
} Shared;

static int rand_range(int lo, int hi_inclusive) {
    // returns a random int in [lo, hi_inclusive]
    return lo + (rand() % (hi_inclusive - lo + 1));
}

int main(void) {
    // Create shared memory (private to this process family)
    int shmid = shmget(IPC_PRIVATE, sizeof(Shared), IPC_CREAT | 0600);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    // Attach in parent
    Shared *ShmPTR = (Shared *)shmat(shmid, NULL, 0);
    if (ShmPTR == (void *)-1) {
        perror("shmat (parent)");
        // If attach fails, try to remove the segment
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    // Initialize shared state
    ShmPTR->BankAccount = 0;
    ShmPTR->Turn = 0; // Parent goes first

    // Seed RNG for parent; child will reseed after fork with its pid
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        // cleanup
        shmdt(ShmPTR);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid == 0) {
        /* ----------------------- CHILD: Poor Student ----------------------- */
        // Child inherits the already-attached ShmPTR (no extra shmget/shmat)
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

        for (int i = 0; i < 25; i++) {
            sleep(rand_range(0, 5));

            // Busy wait until it's the child's turn (Turn == 1)
            while (ShmPTR->Turn != 1) {
                // no-op; strict alternation as required
            }

            int account = ShmPTR->BankAccount;

            int need = rand_range(0, 50);
            printf("Poor Student needs $%d\n", need);

            if (need <= account) {
                account -= need;
                printf("Poor Student: Withdraws $%d / Balance = $%d\n", need, account);
            } else {
                printf("Poor Student: Not Enough Cash ($%d)\n", account);
            }

            // Write back to shared memory & pass turn to parent
            ShmPTR->BankAccount = account;
            ShmPTR->Turn = 0;
        }

        // Child done
        _exit(0);
    } else {
        /* --------------------- PARENT: Dear Old Dad ----------------------- */
        for (int i = 0; i < 25; i++) {
            sleep(rand_range(0, 5));

            // Busy wait until it's the parent's turn (Turn == 0)
            while (ShmPTR->Turn != 0) {
                // no-op; strict alternation as required
            }

            int account = ShmPTR->BankAccount;

            if (account <= 100) {
                // Try to deposit
                int balance = rand_range(0, 100);
                if (balance % 2 == 0) {
                    account += balance;
                    printf("Dear old Dad: Deposits $%d / Balance = $%d\n", balance, account);
                } else {
                    printf("Dear old Dad: Doesn't have any money to give\n");
                }
            } else {
                printf("Dear old Dad: Thinks Student has enough Cash ($%d)\n", account);
            }

            // Write back to shared memory & pass turn to child
            ShmPTR->BankAccount = account;
            ShmPTR->Turn = 1;
        }

        // Wait for child, then cleanup shared memory
        int status = 0;
        waitpid(pid, &status, 0);

        if (shmdt(ShmPTR) == -1) {
            perror("shmdt (parent)");
        }
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl IPC_RMID");
        }

        return 0;
    }
}
