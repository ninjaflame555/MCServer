
// SocketThreads.h

// Interfaces to the cSocketThreads class representing the heart of MCS's client networking.
// This object takes care of network communication, groups sockets into threads and uses as little threads as possible for full read / write support
// For more detail, see http://forum.mc-server.org/showthread.php?tid=327

/*
Additional details:
When a client wants to terminate the connection, they call the RemoveClient() function. This calls the
callback one last time to read all the available outgoing data, putting it in the slot's m_OutgoingData
buffer. Then it marks the slot as having no callback. The socket is kept alive until its outgoing data
queue is empty, then shutdown is called on it and finally the socket is closed after a timeout.
If at any time within this the remote end closes the socket, then the socket is closed directly.
As soon as the socket is closed, the slot is finally removed from the SocketThread.
The graph in $/docs/SocketThreads States.gv shows the state-machine transitions of the slot.
*/





/** How many clients should one thread handle? (must be less than FD_SETSIZE for your platform) */
#define MAX_SLOTS 63





#pragma once

#include "Socket.h"
#include "IsThread.h"




// Check MAX_SLOTS:
#if MAX_SLOTS >= FD_SETSIZE
	#error "MAX_SLOTS must be less than FD_SETSIZE for your platform! (otherwise select() won't work)"
#endif





// fwd:
class cSocket;
class cClientHandle;





class cSocketThreads
{
public:

	// Clients of cSocketThreads must implement this interface to be able to communicate
	class cCallback
	{
	public:
		// Force a virtual destructor in all subclasses:
		virtual ~cCallback() {}
		
		/** Called when data is received from the remote party */
		virtual void DataReceived(const char * a_Data, int a_Size) = 0;
		
		/** Called when data can be sent to remote party; the function is supposed to *append* outgoing data to a_Data */
		virtual void GetOutgoingData(AString & a_Data) = 0;
		
		/** Called when the socket has been closed for any reason */
		virtual void SocketClosed(void) = 0;
	} ;

	
	cSocketThreads(void);
	~cSocketThreads();
	
	/** Add a (socket, client) pair for processing, data from a_Socket is to be sent to a_Client; returns true if successful */
	bool AddClient(const cSocket & a_Socket, cCallback * a_Client);
	
	/** Remove the associated socket and the client from processing.
	The socket is left to send its last outgoing data and is removed only after all its m_Outgoing is sent
	and after the socket is properly shutdown (unless the remote disconnects before that)
	*/
	void RemoveClient(const cCallback * a_Client);
	
	/** Notify the thread responsible for a_Client that the client has something to write */
	void NotifyWrite(const cCallback * a_Client);
	
	/** Puts a_Data into outgoing data queue for a_Client */
	void Write(const cCallback * a_Client, const AString & a_Data);
	
private:

	class cSocketThread :
		public cIsThread
	{
		typedef cIsThread super;
		
	public:
	
		cSocketThread(cSocketThreads * a_Parent);
		~cSocketThread();
		
		// All these methods assume parent's m_CS is locked
		bool HasEmptySlot(void) const {return m_NumSlots < MAX_SLOTS; }
		bool IsEmpty     (void) const {return m_NumSlots == 0; }

		void AddClient   (const cSocket &   a_Socket, cCallback * a_Client);  // Takes ownership of the socket
		bool RemoveClient(const cCallback * a_Client);  // Returns true if removed, false if not found
		bool HasClient   (const cCallback * a_Client) const;
		bool HasSocket   (const cSocket *   a_Socket) const;
		bool NotifyWrite (const cCallback * a_Client);  // Returns true if client handled by this thread
		bool Write       (const cCallback * a_Client, const AString & a_Data);  // Returns true if client handled by this thread
		
		bool Start(void);  // Hide the cIsThread's Start method, we need to provide our own startup to create the control socket
		
		bool IsValid(void) const {return m_ControlSocket2.IsValid(); }  // If the Control socket dies, the thread is not valid anymore
		
	private:
	
		cSocketThreads * m_Parent;
	
		// Two ends of the control socket, the first is select()-ed, the second is written to for notifications
		cSocket m_ControlSocket1;
		cSocket m_ControlSocket2;
		
		// Socket-client-dataqueues-state quadruplets.
		// Manipulation with these assumes that the parent's m_CS is locked
		struct sSlot
		{
			/** The socket is primarily owned by this object */
			cSocket m_Socket;
			
			/** The callback to call for events. May be NULL */
			cCallback * m_Client;
			
			/** If sending writes only partial data, the rest is stored here for another send.
			Also used when the slot is being removed to store the last batch of outgoing data. */
			AString m_Outgoing;
			
			enum eState
			{
				ssNormal,          ///< Normal read / write operations
				ssWritingRestOut,  ///< The client callback was removed, continue to send outgoing data
				ssShuttingDown,    ///< The last outgoing data has been sent, the socket has called shutdown()
				ssShuttingDown2,   ///< The shutdown has been done at least 1 thread loop ago (timeout detection)
				ssRemoteClosed,    ///< The remote end has closed the connection (and we still have a client callback)
			} m_State;
		} ;
		
		sSlot m_Slots[MAX_SLOTS];
		int   m_NumSlots;  // Number of slots actually used
		
		virtual void Execute(void) override;
		
		/** Puts all sockets into the set, along with m_ControlSocket1.
		Only sockets that are able to send and receive data are put in the Set.
		Is a_IsForWriting is true, the ssWritingRestOut sockets are added as well. */
		void PrepareSet(fd_set * a_Set, cSocket::xSocket & a_Highest, bool a_IsForWriting);
		
		void ReadFromSockets(fd_set * a_Read);  // Reads from sockets indicated in a_Read
		void WriteToSockets (fd_set * a_Write);  // Writes to sockets indicated in a_Write
		
		/** Removes those slots in ssShuttingDown2 state, sets those with ssShuttingDown state to ssShuttingDown2 */
		void CleanUpShutSockets(void);
	} ;
	
	typedef std::list<cSocketThread *> cSocketThreadList;
	
	
	cCriticalSection  m_CS;
	cSocketThreadList m_Threads;
} ;



