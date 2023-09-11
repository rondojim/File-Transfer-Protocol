Δημήτριος Ροντογιάννης – sdi1900165
Προγραμματισμός Συστήματος - Εργασία 1η

Στην εργασία αυτή έχουν υλοποιηθεί όλα τα ζητούμενα και επιπρόσθετα, πριν τον τερματισμό του server με SIGINT αποδεσμεύεται η μνήμη, κλείνουν τα sockets, γίνονται destroy τα mutex και cancel τα threads.

-- Client:

Ο Client ξεκινά διαβάζοντας τα arguments από το command line. Δημιουργεί ενα TCP socket και θέτει με την setsockopt SO_REUSEADDR και SO_REUSEPORT, ώστε να μην έχουμε προβλήματα με τα sockets και το port και να μπορούμε να τα ξαναχρησιμοποιήσουμε αφού έχουμε τελειώσει με αυτά. Ύστερα, αφού κάνει connect στο port στέλνει μέσω του socket στον Server το όνομα του directory που θέλει να κάνει copy.
Για κάθε αρχείο του directory θα λάβει το dir_path (τo full path του directory στο οποίο βρίσκεται), το name του αρχείου και το full path του αρχείου (dir_path + name). Επίσης θα λάβει πόσα bytes είναι το αρχείο, πόσα αρχεία συνολικά θα πρέπει να διαβάσει και ποιό είναι το block size με το οποίο στέλνει ο server. Για αυτούς τους unsigned integers θα χρησιμοποιήσει την συνάρτηση ntohl τους μετατρέπει από network byte order σε host byte order.
Κάνει fork() ώστε να δημιουργήσει τους υποκαταλόγους στους οποίους θα κάνει copy το current αρχείο με την mkdir και μετά κάνει remove το αρχείο εαν υπάρχει ήδη.
Μετά όσο δεν έχει διαβάσει όλα τα bytes του αρχείου συνεχίζει να διαβάζει block_size bytes (ή λιγότερα αν βρίσκεται στο τέλος) και τα αντιγράφει στο αρχείο.
Ελέγχει στο εαν έχει διαβάσει όλα τα αρχεία ώστε να τερματίσει.

-- Server:

Ο Server ξεκινά διαβάζοντας τα arguments από το command line. Ομοίως με τον client δημιουργεί ενα TCP socket και θέτει με την setsockopt SO_REUSEADDR και SO_REUSEPORT, ώστε να μην έχουμε προβλήματα με τα sockets και το port και να μπορούμε να τα ξαναχρησιμοποιήσουμε αφού έχουμε τελειώσει με αυτά, και μετά κάλεί την bind.
Σε αυτό το σημείο θα δημιουργήσει τα worker threads και θα αποθηκεύσει τα id τους.
Τα worker threads μπαίνουν σε ενα loop στο οποίο προσπαθούν διαρκώς να κάνουν access σε μια task_queue που θα περιέχει αρχεία για να τα στείλουν στους clients. Ομοίως με το μοντέλο producer/consumer εξασφαλίζεται ότι μόνο ένας worker κάθε φορά θα επεξεργάζεται την task_queue και όσο δεν θα είναι άδεια, χρησιμοποιώντας τον mutex mtx_task_queue και το condition variable cond_task_queue_nonempty.
Στο main thread θα γίνουν accept connections από clients με την connect και για κάθε client θα δημιουργηθεί ενας mutex sock_to_mtx που θα εξασφαλίζει στο μέλλον ότι ανα πάσα στιγμή μόνο ένα thread θα επικοινωνεί μέσω του socket με αυτόν. Για διευκόλυνση του server θα κάνει map το socket με αυτόν το mutex ώστε στο μέλλον να τον βρίσκει γρήγορα δεδομένου του socket.
Ύστερα θα δημιουργήσει ένα communication thread που θα αναλάβει την επικοινωνία με τον client και θα τροφοδοτήσει την task_queue με tasks για τους workers.
Στο communication thread αρχικά εξασφαλίζουμε με τον mutex για τη συγκεκριμένη socket ότι θα υπάρχει μόνο αυτό το thread που θα επικοινωνεί εκείνη την στιγμή και διαβάζουμε το pathname του directory. Χρησιμοποιούμε την αναδρομική συνάρτηση get_all_names για να πάρουμε τα ονόματα και τα μονοπάτια των αρχείων που υπάρχουν σε αυτόν τον κατάλογο. Ύστερα, για κάθε ένα αρχείο θα κάνουμε push στην queue (ενώ έχουμε εξασφαλίσει ότι μόνο αυτό το thread έχει πρόσβαση και ότι δεν είναι γεμάτη η task_queue με το condition variable cond_task_queue_nonfull) ένα struct που περιέχει πληροφορίες όπως το dir_path, name, fullname, client_socket, filebytes, numoffiles (του directory που έστειλε ο client), block_size που συμβολίζουν ότι λέει το όνομα τους.
Ταυτόχρονα οι workers θα παίρνουν τα tasks από την queue και θα στέλνουν τις πληροφορίες της struct στον client (αφού έχει μετατρέψει τους unsigned integers με την htonl από host byte order σε network byte order). 
Μετά θα ανοίξει ο worker το αρχείο και θα στείλει τα περιεχόμενα του ανα block_size.

!!!!! Σημείωση: στην εργασία δεν έχουν χρησιμοποιηθεί οι συναρτήσεις read και write για strings για N bytes γιατί σε κάποιες περιπτώσεις διαβάζουν / γράφουν λιγότερα. Επομένως, χρησιμοποιούνται οι myread_s και mywrite_s που κάνουν χρήση των read και write όσο μένουν bytes να διαβαστούν / σταλθούν.

Κάνουμε ταυτόχρονα compile (remoteClient, dataServer) με make all.
Καθαρίζουμε με make clean

