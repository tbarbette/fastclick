/*
 * rbt.hh - Set of methods and structures used to create and manage red black trees
 *
 * This file only declares and documents the methods that can be used publicly to manage RBTs
 * For helper methods used internally, please refer to rbt.cc
 *
 * Romain Gaillard - Adapted from the Red Black Tree implementation of Emin Martinian.
 */

#ifndef MIDDLEBOX_RBT_HH
#define MIDDLEBOX_RBT_HH

CLICK_DECLS

class RBTManager;

/**
 * Adapted from the Red Black Tree implementation of Emin Martinian:
 * (http://web.mit.edu/~emin/www.old/source_code/red_black_tree/index.html)
 * (http://web.mit.edu/~emin/www.old/source_code/red_black_tree/LICENSE)
 */

/**
* Structure used to represent a node in the RBT
*/
typedef struct rb_red_blk_node {
    void* key;
    void* info;
    int red; /* if red=0 then the node is black */
    struct rb_red_blk_node* left;
    struct rb_red_blk_node* right;
    struct rb_red_blk_node* parent;
} rb_red_blk_node;

/**
 * Structure used to represent a RBT
 */
typedef struct rb_red_blk_tree {
    RBTManager* manager; // Pointer to the RBTManager used to compare keys and allocate memory
    /*  A sentinel is used for root and for nil.  These sentinels are */
    /*  created when RBTreeCreate is called.  root->left should always */
    /*  point to the node which is the root of the tree.  nil points to a */
    /*  node which should always be black but has aribtrary children and */
    /*  parent and no key or info.  The point of using these sentinels is so */
    /*  that the root and nil nodes do not require special cases in the code */
    rb_red_blk_node* root;
    rb_red_blk_node* nil;
} rb_red_blk_tree;


/** @class RBTManager
 * @brief This abstract class is used to define the behaviour of a RBT manager.
 * The manager must take care of memory management as well as providing
 * methods to handle the objects managed by the tree.
*/
class RBTManager
{
public:
    /**
     * @brief Compare two keys in the RBT.
     * @param first Pointer to the first key
     * @param second Pointer to the second key
     * @return 1 if *first > *second, -1 if *first < *second and 0 otherwise
     */
    virtual int compareKeys(const void* first, const void* second) = 0;

    /**
     * @brief Print a key
     * @param key Pointer to the key
     */
    virtual void printKey(const void* key) = 0;

    /**
     * @brief Print the info associated to a key in a node
     * @param info Pointer to the info
     */
    virtual void printInfo(void* info) = 0;

    /**
     * @brief Allocate and return memory for a node
     * @return A pointer to the allocated node
     */
    virtual rb_red_blk_node* allocateNode(void) = 0;

    /**
     * @brief Allocate and return memory for a tree
     * @return A pointer to the allocated tree
     */
    virtual rb_red_blk_tree* allocateTree(void) = 0;

    /**
     * @brief Free the memory used by a tree
     * @param tree Pointer to the tree
     */
    virtual void freeTree(rb_red_blk_tree* tree) = 0;

    /**
     * @brief Free the memory used by a node
     * @param node Pointer to the node
     */
    virtual void freeNode(rb_red_blk_node* node) = 0;

    /**
     * @brief Free the memory used by a key
     * @param key Pointer to the key
     */
    virtual void freeKey(void* key) = 0;

    /**
     * @brief Free the memory used by an info
     * @param info Pointer to the info
     */
    virtual void freeInfo(void* info) = 0;
};

/**
 * @brief Create a RBT
 * @param manager Pointer to the RBTManager of the tree
 * @return Pointer to the newly created RBT
 */
rb_red_blk_tree* RBTreeCreate(RBTManager* manager);

/**
 * @brief Insert a node in the RBT
 * @param tree Pointer to the tree
 * @param key Pointer to the key
 * @param info Pointer to the info
 * @return Pointer to the newly inserted node
 */
rb_red_blk_node * RBTreeInsert(rb_red_blk_tree* tree, void* key, void* info);

/**
 * @brief Print the content of a RBT in the console
 * @param tree A pointer to the tree to print
 */
void RBTreePrint(rb_red_blk_tree* tree);

/**
 * @brief Remove a node from a RBT
 * @param tree A pointer to the tree in which the node is
 * @param node A pointer to the node to remove
 */
void RBDelete(rb_red_blk_tree* tree, rb_red_blk_node* node);

/**
 * @brief Destruct a RBT and free the associated memory
 * @param tree A pointer to the tree to destruct
 */
void RBTreeDestroy(rb_red_blk_tree* tree);

/**
 * @brief Return the predecessor of a node in a RBT
 * @param tree A pointer to the tree
 * @param node A pointer to the node
 * @return A pointer to the predecessor of the node or tree->nil if the node has no predecessors
 */
rb_red_blk_node* TreePredecessor(rb_red_blk_tree* tree, rb_red_blk_node* node);

/**
 * @brief Return the successor of a node in a RBT
 * @param tree A pointer to the tree
 * @param node A pointer to the node
 * @return A pointer to the successor of the node or tree->nil if the node has no successors
 */
rb_red_blk_node* TreeSuccessor(rb_red_blk_tree*, rb_red_blk_node*);

/**
 * @brief Search a key in the RBT
 * @param tree A pointer to the tree
 * @param key A pointer to the key
 * @return A pointer to the node containing the key or tree->nil if the key is not in the tree
 */
rb_red_blk_node* RBExactQuery(rb_red_blk_tree* tree, void* key);

/**
 * @brief Search the node with the largest key less or equal to the given one
 *
 * Complexity: O(log(n)) where n is the number of elements in the tree
 *
 * @param tree A pointer to the tree
 * @param key A pointer to the key
 * @return A pointer to the node containing the largest key in the tree less or equal to the given
 * one or tree->nil if the tree is empty
 */
rb_red_blk_node* RBFindElementGreatestBelow(rb_red_blk_tree* tree, void* key);

/**
 * @brief Search the minimum key in the tree
 *
 * Complexity: O(log(n)) where n is the number of elements in the tree
 *
 * @param tree A pointer to the tree
 * @param key A pointer to the key
 * @return A pointer to the node containing the minimum key or tree->nil if the tree is empty
 */
rb_red_blk_node* RBMin(rb_red_blk_tree* tree);

/**
 * @brief Search the maximum key in the tree
 *
 * Complexity: O(log(n)) where n is the number of elements in the tree
 *
 * @param tree A pointer to the tree
 * @param key A pointer to the key
 * @return A pointer to the node containing the maximum key or tree->nil if the tree is empty
 */
rb_red_blk_node* RBMax(rb_red_blk_tree* tree);

/**
 * @brief Prune the RBT and remove keys under a given threshold. Every value between the minimum
 * value of the tree and the predecessor of the predecessor of the node obtained by applying
 * RBFindElementGreatestBelow on the given key will be removed.
 *
 * Complexity: O(k * log(n)) where k is the number of elements with a key < q
 * and n is the total number of elements in the tree
 *
 * @param tree A pointer to the tree
 * @param key A pointer to the key acting as a threshold
 */
void RBPrune(rb_red_blk_tree* tree, void* key);

CLICK_ENDDECLS

#endif
